#include "Combat/Component/PP_CombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Combat/Data/PP_CombatAbilityData.h"
#include "Combat/FunctionLibrary/PP_CombatFunctionLibrary.h"
#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameplayEffect.h"
#include "TimerManager.h"

namespace
{
constexpr int32 PPCombatMaxAbilitySystemRetries = 30;
constexpr float PPCombatAbilitySystemRetryInterval = 0.1f;
}

UPP_CombatComponent::UPP_CombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPP_CombatComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshAnimInstance();
	TryInitializeAbilitySystem();
}

void UPP_CombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AbilitySystemRetryTimer);
	}

	ApplyCrowdControlState(false);
	ApplyPawnCollisionIgnoreState(false);
	ClearTagListeners();
	ClearReactionTimers();

	Super::EndPlay(EndPlayReason);
}

bool UPP_CombatComponent::IsCrowdControlActive() const
{
	return AbilitySystemComponent && CrowdControlTag.IsValid() &&
		AbilitySystemComponent->HasMatchingGameplayTag(CrowdControlTag);
}

bool UPP_CombatComponent::IsIgnoringPawnCollision() const
{
	return AbilitySystemComponent && IgnorePawnCollisionTag.IsValid() &&
		AbilitySystemComponent->HasMatchingGameplayTag(IgnorePawnCollisionTag);
}

void UPP_CombatComponent::ApplyPawnCollisionIgnoreState(const bool bShouldIgnore)
{
	if (bPawnCollisionIgnoreEnforced == bShouldIgnore) return;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule) return;

	if (bShouldIgnore)
	{
		// Save the character's configured response once so overlapping effects restore it correctly.
		SavedPawnCollisionResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		bPawnCollisionIgnoreEnforced = true;
		return;
	}

	Capsule->SetCollisionResponseToChannel(ECC_Pawn, SavedPawnCollisionResponse);
	bPawnCollisionIgnoreEnforced = false;
}

void UPP_CombatComponent::ApplyCrowdControlState(const bool bShouldEnforce)
{
	if (bCrowdControlEnforced == bShouldEnforce) return;
	bCrowdControlEnforced = bShouldEnforce;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;

	// Controller move-input suppression affects players and locally controlled AI, but never camera input.
	if (AController* Controller = Character->GetController())
	{
		Controller->SetIgnoreMoveInput(bShouldEnforce);

		// Stop current player/AI path following immediately; camera/look input remains enabled.
		if (bShouldEnforce)
		{
			Controller->StopMovement();
		}
	}

	if (UPP_CharacterMovementComponent* MoveComp =
		Cast<UPP_CharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		// This independent flag cannot be cleared accidentally by ability montage state restoration.
		MoveComp->SetCrowdControlMovementInputSuppressed(bShouldEnforce);
		if (bShouldEnforce)
		{
			MoveComp->StopMovementImmediately();
		}
	}
}

bool UPP_CombatComponent::IsBlockingActive() const
{
	return AbilitySystemComponent && BlockingTag.IsValid() &&
		AbilitySystemComponent->HasMatchingGameplayTag(BlockingTag);
}

bool UPP_CombatComponent::CanBlockAttackFrom(const AActor* AttackerActor, const float BlockAngleDegrees) const
{
	const AActor* DefenderActor = GetOwner();
	if (!DefenderActor || !AttackerActor || !IsBlockingActive()) return false;

	// Work in the horizontal plane so height differences do not change a combat-facing check.
	FVector ToAttacker = AttackerActor->GetActorLocation() - DefenderActor->GetActorLocation();
	ToAttacker.Z = 0.0f;
	if (!ToAttacker.Normalize()) return true;

	FVector DefenderForward = DefenderActor->GetActorForwardVector();
	DefenderForward.Z = 0.0f;
	DefenderForward.Normalize();

	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(BlockAngleDegrees, 0.0f, 180.0f)));
	return FVector::DotProduct(DefenderForward, ToAttacker) >= MinimumDot;
}

bool UPP_CombatComponent::IsParryingActive() const
{
	return AbilitySystemComponent && ParryingTag.IsValid() &&
		AbilitySystemComponent->HasMatchingGameplayTag(ParryingTag);
}

bool UPP_CombatComponent::IsDodgingActive() const
{
	return AbilitySystemComponent && DodgingTag.IsValid() &&
		AbilitySystemComponent->HasMatchingGameplayTag(DodgingTag);
}

EPP_SuperArmorLevel UPP_CombatComponent::GetSuperArmorLevel() const
{
	if (!AbilitySystemComponent) return EPP_SuperArmorLevel::None;
	if (SuperArmorTag3.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(SuperArmorTag3))
		return EPP_SuperArmorLevel::SuperArmor3;
	if (SuperArmorTag2.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(SuperArmorTag2))
		return EPP_SuperArmorLevel::SuperArmor2;
	if (SuperArmorTag1.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(SuperArmorTag1))
		return EPP_SuperArmorLevel::SuperArmor1;
	return EPP_SuperArmorLevel::None;
}

bool UPP_CombatComponent::DoesCurrentSuperArmorIgnoreDamage() const
{
	return MinimumSuperArmorLevelToIgnoreDamage != EPP_SuperArmorLevel::None &&
		GetSuperArmorLevel() >= MinimumSuperArmorLevelToIgnoreDamage;
}

void UPP_CombatComponent::ApplySuccessfulBlockEffects(AActor* AttackerActor) const
{
	AActor* DefenderActor = GetOwner();
	if (!DefenderActor || !DefenderActor->HasAuthority()) return;

	ApplyGameplayEffectToActor(AttackerActor, AttackerBlockedEffectClass, DefenderActor);
	ApplyGameplayEffectToActor(DefenderActor, DefenderBlockSuccessEffectClass, AttackerActor);
}

void UPP_CombatComponent::ApplySuccessfulParryEffects(AActor* AttackerActor) const
{
	AActor* DefenderActor = GetOwner();
	if (!DefenderActor || !DefenderActor->HasAuthority()) return;
	ApplyGameplayEffectToActor(AttackerActor, AttackerParriedEffectClass, DefenderActor);
	ApplyGameplayEffectToActor(DefenderActor, DefenderParrySuccessEffectClass, AttackerActor);
}

void UPP_CombatComponent::ApplySuccessfulDodgeEffects(AActor* AttackerActor) const
{
	AActor* DefenderActor = GetOwner();
	if (!DefenderActor || !DefenderActor->HasAuthority()) return;
	ApplyGameplayEffectToActor(AttackerActor, AttackerDodgedEffectClass, DefenderActor);
	ApplyGameplayEffectToActor(DefenderActor, DefenderDodgeSuccessEffectClass, AttackerActor);
}

void UPP_CombatComponent::ApplySuccessfulSuperArmorEffects(AActor* AttackerActor) const
{
	AActor* DefenderActor = GetOwner();
	if (!DefenderActor || !DefenderActor->HasAuthority()) return;
	ApplyGameplayEffectToActor(AttackerActor, AttackerSuperArmoredEffectClass, DefenderActor);
	ApplyGameplayEffectToActor(DefenderActor, DefenderSuperArmorSuccessEffectClass, AttackerActor);
}

void UPP_CombatComponent::ApplyGameplayEffectToActor(AActor* RecipientActor,
	const TSubclassOf<UGameplayEffect>& EffectClass, AActor* InstigatorActor) const
{
	if (!RecipientActor || !EffectClass) return;

	UAbilitySystemComponent* RecipientASC =
		UPP_CombatFunctionLibrary::GetAbilitySystemComponent(RecipientActor);
	if (!RecipientASC || !RecipientASC->IsOwnerActorAuthoritative()) return;

	FGameplayEffectContextHandle Context = RecipientASC->MakeEffectContext();
	Context.AddInstigator(InstigatorActor, InstigatorActor);
	Context.AddSourceObject(GetOwner());

	const FGameplayEffectSpecHandle Spec = RecipientASC->MakeOutgoingSpec(EffectClass, 1.0f, Context);
	if (Spec.IsValid())
	{
		RecipientASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

void UPP_CombatComponent::RefreshAnimInstance()
{
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
		{
			AnimInstance = Mesh->GetAnimInstance();
		}
	}
}

bool UPP_CombatComponent::ResolveAbilitySystemComponent()
{
	if (AbilitySystemComponent) return true;

	AbilitySystemComponent = UPP_CombatFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	return AbilitySystemComponent != nullptr;
}

void UPP_CombatComponent::InitializeAbilityActorInfoIfNeeded() const
{
	if (!AbilitySystemComponent || !GetOwner()) return;

	if (AbilitySystemComponent->AbilityActorInfo.IsValid() &&
		AbilitySystemComponent->AbilityActorInfo->AvatarActor.Get() == GetOwner())
	{
		return;
	}

	AActor* AbilityOwner = AbilitySystemComponent->GetOwner();
	AbilitySystemComponent->InitAbilityActorInfo(AbilityOwner ? AbilityOwner : GetOwner(), GetOwner());
}

void UPP_CombatComponent::TryInitializeAbilitySystem()
{
	if (!ResolveAbilitySystemComponent())
	{
		ScheduleAbilitySystemRetry();
		return;
	}

	AbilitySystemRetryCount = 0;

	InitializeAbilityActorInfoIfNeeded();
	GrantAbilities();
	RegisterTagListeners();
}

void UPP_CombatComponent::ScheduleAbilitySystemRetry()
{
	if (++AbilitySystemRetryCount > PPCombatMaxAbilitySystemRetries) return;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AbilitySystemRetryTimer,
			this,
			&UPP_CombatComponent::TryInitializeAbilitySystem,
			PPCombatAbilitySystemRetryInterval,
			false);
	}
}

void UPP_CombatComponent::ClearTagListeners()
{
	if (AbilitySystemComponent)
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Entry : TagListenerHandles)
		{
			if (!Entry.Key.IsValid() || !Entry.Value.IsValid()) continue;

			AbilitySystemComponent
				->RegisterGameplayTagEvent(Entry.Key, EGameplayTagEventType::AnyCountChange)
				.Remove(Entry.Value);
		}
	}

	TagListenerHandles.Reset();
}

void UPP_CombatComponent::ClearReactionTimers()
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<FGameplayTag, FTimerHandle>& Entry : AbilityTimers)
		{
			World->GetTimerManager().ClearTimer(Entry.Value);
		}

		for (TPair<FName, FTimerHandle>& Entry : RemoveEffectTimers)
		{
			World->GetTimerManager().ClearTimer(Entry.Value);
		}

		for (TPair<FGameplayTag, FTimerHandle>& Entry : ApplyEffectTimers)
		{
			World->GetTimerManager().ClearTimer(Entry.Value);
		}
	}

	AbilityTimers.Reset();
	RemoveEffectTimers.Reset();
	ApplyEffectTimers.Reset();
	AppliedEffectHandles.Reset();
}

void UPP_CombatComponent::GrantAbilities()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	ResolveAbilitySystemComponent();
	if (!AbilitySystemComponent || !AbilityData) return;

	InitializeAbilityActorInfoIfNeeded();

	const FGameplayTag InputTagRoot = FGameplayTag::RequestGameplayTag(TEXT("InputTag"), false);
	const FGameplayTag SyncInputRoot = FGameplayTag::RequestGameplayTag(TEXT("SyncInput"), false);

	for (const FPP_CombatAbilityGroup& Group : AbilityData->AbilityGroups)
	{
		if (!Group.bEnabled) continue;

		for (const TSubclassOf<UGameplayAbility>& AbilityClass : Group.Abilities)
		{
			if (!AbilityClass || HasAbility(AbilityClass)) continue;

			FGameplayAbilitySpec Spec(AbilityClass, 1);

			const UGameplayAbility* AbilityCDO = AbilityClass->GetDefaultObject<UGameplayAbility>();
			const FGameplayTagContainer& AssetTags = AbilityCDO->GetAssetTags();

			FGameplayTagContainer InputTags;
			for (const FGameplayTag& Tag : AssetTags)
			{
				if ((Tag.MatchesTag(InputTagRoot) && Tag != InputTagRoot) ||
					(Tag.MatchesTag(SyncInputRoot) && Tag != SyncInputRoot))
				{
					InputTags.AddTag(Tag);
				}
			}

			Spec.GetDynamicSpecSourceTags().AppendTags(InputTags);
			AbilitySystemComponent->GiveAbility(Spec);
		}
	}
}

void UPP_CombatComponent::RegisterTagListeners()
{
	if (!ResolveAbilitySystemComponent())
	{
		ScheduleAbilitySystemRetry();
		return;
	}

	if (!TagReactionData && AnimBoolBindings.IsEmpty() && !CrowdControlTag.IsValid() &&
		!IgnorePawnCollisionTag.IsValid()) return;

	ClearTagListeners();

	if (!AnimInstance)
	{
		RefreshAnimInstance();
	}

	if (AnimInstance)
	{
		for (FPP_CombatAnimBoolBinding& Binding : AnimBoolBindings)
		{
			Binding.CachedBoolProperty = nullptr;
			if (Binding.AnimBoolName.IsNone()) continue;

			if (FProperty* Found = AnimInstance->GetClass()->FindPropertyByName(Binding.AnimBoolName))
			{
				Binding.CachedBoolProperty = CastField<FBoolProperty>(Found);
			}
		}
	}

	TSet<FGameplayTag> WatchedTags;
	if (CrowdControlTag.IsValid()) WatchedTags.Add(CrowdControlTag);
	if (IgnorePawnCollisionTag.IsValid()) WatchedTags.Add(IgnorePawnCollisionTag);

	if (TagReactionData)
	{
		for (const FPP_CombatTagReactionBinding& Reaction : TagReactionData->Reactions)
		{
			if (Reaction.TriggerTag.IsValid())
			{
				WatchedTags.Add(Reaction.TriggerTag);
			}
		}
	}

	for (const FPP_CombatAnimBoolBinding& Binding : AnimBoolBindings)
	{
		TArray<FGameplayTag> Tags;
		Binding.Tags.GetGameplayTagArray(Tags);

		for (const FGameplayTag& Tag : Tags)
		{
			if (Tag.IsValid())
			{
				WatchedTags.Add(Tag);
			}
		}
	}

	for (const FGameplayTag& Tag : WatchedTags)
	{
		FDelegateHandle Handle = AbilitySystemComponent
			->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddUObject(this, &UPP_CombatComponent::OnTagChanged);
		TagListenerHandles.Add(Tag, Handle);
	}

	for (const FPP_CombatAnimBoolBinding& Binding : AnimBoolBindings)
	{
		SetAnimBool(Binding, IsAnimBoolActive(Binding));
	}

	ApplyCrowdControlState(IsCrowdControlActive());
	ApplyPawnCollisionIgnoreState(IsIgnoringPawnCollision());
}

void UPP_CombatComponent::OnTagChanged(FGameplayTag Tag, int32 NewCount)
{
	if (!ResolveAbilitySystemComponent())
	{
		ScheduleAbilitySystemRetry();
		return;
	}

	const bool bAdded = NewCount > 0;
	if (CrowdControlTag.IsValid() && Tag.MatchesTag(CrowdControlTag))
	{
		ApplyCrowdControlState(bAdded);
	}
	if (IgnorePawnCollisionTag.IsValid() && Tag.MatchesTag(IgnorePawnCollisionTag))
	{
		ApplyPawnCollisionIgnoreState(bAdded);
	}

	if (TagReactionData)
	{
		for (const FPP_CombatTagReactionBinding& Reaction : TagReactionData->Reactions)
		{
			if (!Reaction.TriggerTag.IsValid()) continue;
			if (!Tag.MatchesTag(Reaction.TriggerTag)) continue;

			const bool bShouldRun =
				Reaction.Policy == EPP_CombatTagReactionPolicy::Both ||
				(Reaction.Policy == EPP_CombatTagReactionPolicy::OnAdd && bAdded) ||
				(Reaction.Policy == EPP_CombatTagReactionPolicy::OnRemove && !bAdded);

			if (!bShouldRun) continue;

			QueueEffectRemove(Reaction, Tag, AbilitySystemComponent);
			QueueEffectApply(Reaction, Tag, AbilitySystemComponent);
			QueueAbilityActivation(Reaction, Tag, AbilitySystemComponent);
		}
	}

	for (const FPP_CombatAnimBoolBinding& Binding : AnimBoolBindings)
	{
		if (Binding.Tags.HasTagExact(Tag))
		{
			SetAnimBool(Binding, IsAnimBoolActive(Binding));
		}
	}
}

void UPP_CombatComponent::SetAnimBool(const FPP_CombatAnimBoolBinding& Binding, bool bValue)
{
	if (!AnimInstance || !Binding.CachedBoolProperty) return;

	void* Ptr = Binding.CachedBoolProperty->ContainerPtrToValuePtr<void>(AnimInstance);
	Binding.CachedBoolProperty->SetPropertyValue(Ptr, bValue);
}

bool UPP_CombatComponent::IsAnimBoolActive(const FPP_CombatAnimBoolBinding& Binding) const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasAnyMatchingGameplayTags(Binding.Tags);
}

bool UPP_CombatComponent::HasAbility(const TSubclassOf<UGameplayAbility>& AbilityClass) const
{
	if (!AbilitySystemComponent) return false;

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
		{
			return true;
		}
	}

	return false;
}

void UPP_CombatComponent::QueueAbilityActivation(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC)
{
	if (!Binding.Ability.AbilityTag.IsValid() || !ASC) return;

	auto Fn = [ASC, Binding]
	{
		ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(Binding.Ability.AbilityTag));
	};

	FTimerHandle& Handle = AbilityTimers.FindOrAdd(TriggeredTag);
	ExecuteDelayed(Fn, Binding.Ability.DelaySeconds, Handle);
}

void UPP_CombatComponent::QueueEffectRemove(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC)
{
	if (Binding.Effects.Remove.Num() <= 0 || !ASC) return;

	const FName RemoveTimerKey = GetRemoveTimerKey(Binding, TriggeredTag);
	auto Fn = [this, ASC, Binding, RemoveTimerKey]
	{
		if (TArray<FActiveGameplayEffectHandle>* Handles = AppliedEffectHandles.Find(RemoveTimerKey))
		{
			for (const FActiveGameplayEffectHandle& Handle : *Handles)
			{
				if (Handle.IsValid())
				{
					ASC->RemoveActiveGameplayEffect(Handle);
				}
			}

			AppliedEffectHandles.Remove(RemoveTimerKey);
		}

		for (const TSubclassOf<UGameplayEffect>& Effect : Binding.Effects.Remove)
		{
			if (!Effect) continue;

			FGameplayEffectQuery Query;
			Query.EffectDefinition = Effect;
			ASC->RemoveActiveEffects(Query);
		}
	};

	FTimerHandle& Handle = RemoveEffectTimers.FindOrAdd(RemoveTimerKey);
	ExecuteDelayed(Fn, Binding.Effects.RemoveDelaySeconds, Handle);
}

FName UPP_CombatComponent::GetRemoveTimerKey(const FPP_CombatTagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const
{
	return Binding.Effects.RemoveTimerKey.IsNone()
		? TriggeredTag.GetTagName()
		: Binding.Effects.RemoveTimerKey;
}

void UPP_CombatComponent::QueueEffectApply(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC)
{
	if (Binding.Effects.Apply.Num() <= 0 || !ASC) return;

	const FName RemoveTimerKey = GetRemoveTimerKey(Binding, TriggeredTag);
	auto Fn = [this, ASC, Binding, RemoveTimerKey]
	{
		for (const TSubclassOf<UGameplayEffect>& Effect : Binding.Effects.Apply)
		{
			if (!Effect) continue;

			FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
			Ctx.AddSourceObject(GetOwner());

			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Effect, 1.f, Ctx);
			if (Spec.IsValid())
			{
				const FActiveGameplayEffectHandle AppliedHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
				if (AppliedHandle.IsValid())
				{
					AppliedEffectHandles.FindOrAdd(RemoveTimerKey).Add(AppliedHandle);
				}
			}
		}
	};

	FTimerHandle& Handle = ApplyEffectTimers.FindOrAdd(TriggeredTag);
	ExecuteDelayed(Fn, Binding.Effects.ApplyDelaySeconds, Handle);
}

template<typename Func>
void UPP_CombatComponent::ExecuteDelayed(Func InFunction, float DelaySeconds, FTimerHandle& TimerHandle)
{
	if (DelaySeconds <= 0.f)
	{
		InFunction();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);

		FTimerDelegate Delegate;
		Delegate.BindLambda(InFunction);

		World->GetTimerManager().SetTimer(TimerHandle, Delegate, DelaySeconds, false);
	}
}
