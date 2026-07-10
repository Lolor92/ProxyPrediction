#include "Combat/Component/PP_CombatComponent.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Combat/Data/PP_CombatAbilityData.h"
#include "Combat/FunctionLibrary/PP_CombatFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
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

	ClearTagListeners();
	ClearReactionTimers();

	Super::EndPlay(EndPlayReason);
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

	if (!TagReactionData && AnimBoolBindings.IsEmpty()) return;

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
}

void UPP_CombatComponent::OnTagChanged(FGameplayTag Tag, int32 NewCount)
{
	if (!ResolveAbilitySystemComponent())
	{
		ScheduleAbilitySystemRetry();
		return;
	}

	const bool bAdded = NewCount > 0;

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

	auto Fn = [ASC, Binding]
	{
		for (const TSubclassOf<UGameplayEffect>& Effect : Binding.Effects.Remove)
		{
			if (!Effect) continue;

			FGameplayEffectQuery Query;
			Query.EffectDefinition = Effect;
			ASC->RemoveActiveEffects(Query);
		}
	};

	FTimerHandle& Handle = RemoveEffectTimers.FindOrAdd(GetRemoveTimerKey(Binding, TriggeredTag));
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

	auto Fn = [this, ASC, Binding]
	{
		for (const TSubclassOf<UGameplayEffect>& Effect : Binding.Effects.Apply)
		{
			if (!Effect) continue;

			FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
			Ctx.AddSourceObject(GetOwner());

			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Effect, 1.f, Ctx);
			if (Spec.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
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
