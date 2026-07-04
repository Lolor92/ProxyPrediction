#include "Component/SyncCombatComponent.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "FunctionLibrary/SyncCombatFunctionLibrary.h"
#include "Data/SCAbilityData.h"

namespace
{
constexpr int32 SyncCombatMaxAbilitySystemRetries = 30;
constexpr float SyncCombatAbilitySystemRetryInterval = 0.1f;
}

USyncCombatComponent::USyncCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USyncCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshAnimInstance();
	TryInitializeAbilitySystem();
}

void USyncCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AbilitySystemRetryTimer);
	}

	ClearTagListeners();
	ClearReactionTimers();

	Super::EndPlay(EndPlayReason);
}

void USyncCombatComponent::RefreshAnimInstance()
{
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
		{
			AnimInstance = Mesh->GetAnimInstance();
		}
	}
}

bool USyncCombatComponent::ResolveAbilitySystemComponent()
{
	if (AbilitySystemComponent) return true;

	AbilitySystemComponent = USyncCombatFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	return AbilitySystemComponent != nullptr;
}

void USyncCombatComponent::InitializeAbilityActorInfoIfNeeded() const
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

void USyncCombatComponent::TryInitializeAbilitySystem()
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

void USyncCombatComponent::ScheduleAbilitySystemRetry()
{
	if (++AbilitySystemRetryCount > SyncCombatMaxAbilitySystemRetries) return;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AbilitySystemRetryTimer,
			this,
			&USyncCombatComponent::TryInitializeAbilitySystem,
			SyncCombatAbilitySystemRetryInterval,
			false);
	}
}

void USyncCombatComponent::ClearTagListeners()
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

void USyncCombatComponent::ClearReactionTimers()
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

void USyncCombatComponent::GrantAbilities()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	ResolveAbilitySystemComponent();
	if (!AbilitySystemComponent || !AbilityData) return;

	InitializeAbilityActorInfoIfNeeded();

	const FGameplayTag InputTagRoot = FGameplayTag::RequestGameplayTag(TEXT("InputTag"), false);
	const FGameplayTag SyncInputRoot = FGameplayTag::RequestGameplayTag(TEXT("SyncInput"), false);

	for (const FSCAbilityGroup& Group : AbilityData->AbilityGroups)
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
				if ((Tag.MatchesTag(InputTagRoot)  && Tag != InputTagRoot) ||
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

void USyncCombatComponent::RegisterTagListeners()
{
	if (!ResolveAbilitySystemComponent())
	{
		ScheduleAbilitySystemRetry();
		return;
	}

	if (!TagReactionData && AnimBoolBindings.IsEmpty()) return;

	ClearTagListeners();

	// Ensure AnimInstance exists (if mesh swapped / BeginPlay timing)
	if (!AnimInstance)
	{
		RefreshAnimInstance();
	}

	// Cache bool properties from AnimInstance class
	if (AnimInstance)
	{
		for (FAnimBoolBinding& Binding : AnimBoolBindings)
		{
			Binding.CachedBoolProperty = nullptr;
			if (Binding.AnimBoolName.IsNone()) continue;

			if (FProperty* Found = AnimInstance->GetClass()->FindPropertyByName(Binding.AnimBoolName))
			{
				Binding.CachedBoolProperty = CastField<FBoolProperty>(Found);
			}
		}
	}

	// Union of watched tags (reactions + anim bool bindings)
	TSet<FGameplayTag> WatchedTags;

	if (TagReactionData)
	{
		for (const FTagReactionBinding& Reaction : TagReactionData->Reactions)
		{
			if (Reaction.TriggerTag.IsValid())
			{
				WatchedTags.Add(Reaction.TriggerTag);
			}
		}
	}

	for (const FAnimBoolBinding& Binding : AnimBoolBindings)
	{
		TArray<FGameplayTag> Tags;
		Binding.Tags.GetGameplayTagArray(Tags);

		for (const FGameplayTag& T : Tags)
		{
			if (T.IsValid())
			{
				WatchedTags.Add(T);
			}
		}
	}

	for (const FGameplayTag& Tag : WatchedTags)
	{
		FDelegateHandle Handle = AbilitySystemComponent
			->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddUObject(this, &USyncCombatComponent::OnTagChanged);
		TagListenerHandles.Add(Tag, Handle);
	}

	// Initial sync (handles tags already present at BeginPlay)
	for (const FAnimBoolBinding& Binding : AnimBoolBindings)
	{
		SetAnimBool(Binding, IsAnimBoolActive(Binding));
	}
}

void USyncCombatComponent::OnTagChanged(FGameplayTag Tag, int32 NewCount)
{
	if (!ResolveAbilitySystemComponent())
	{
		ScheduleAbilitySystemRetry();
		return;
	}

	const bool bAdded = (NewCount > 0);

	// 1) Tag reactions (effects / ability activation)
	if (TagReactionData)
	{
		for (const FTagReactionBinding& Reaction : TagReactionData->Reactions)
		{
			if (!Reaction.TriggerTag.IsValid()) continue;
			if (!Tag.MatchesTag(Reaction.TriggerTag)) continue;

			const bool bShouldRun =
				(Reaction.Policy == ETagReactionPolicy::Both) ||
				(Reaction.Policy == ETagReactionPolicy::OnAdd    && bAdded) ||
				(Reaction.Policy == ETagReactionPolicy::OnRemove && !bAdded);

			if (!bShouldRun) continue;

			QueueEffectRemove(Reaction, Tag, AbilitySystemComponent);
			QueueEffectApply (Reaction, Tag, AbilitySystemComponent);
			QueueAbilityActivation(Reaction, Tag, AbilitySystemComponent);
		}
	}

	// 2) Anim bool updates (exact match for the changed tag)
	for (const FAnimBoolBinding& Binding : AnimBoolBindings)
	{
		if (Binding.Tags.HasTagExact(Tag))
		{
			SetAnimBool(Binding, IsAnimBoolActive(Binding));
		}
	}
}

void USyncCombatComponent::SetAnimBool(const FAnimBoolBinding& Binding, bool bValue)
{
	if (!AnimInstance || !Binding.CachedBoolProperty) return;

	void* Ptr = Binding.CachedBoolProperty->ContainerPtrToValuePtr<void>(AnimInstance);
	Binding.CachedBoolProperty->SetPropertyValue(Ptr, bValue);
}

bool USyncCombatComponent::IsAnimBoolActive(const FAnimBoolBinding& Binding) const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasAnyMatchingGameplayTags(Binding.Tags);
}

bool USyncCombatComponent::HasAbility(const TSubclassOf<UGameplayAbility>& AbilityClass) const
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

void USyncCombatComponent::QueueAbilityActivation(const FTagReactionBinding& Binding, const FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC)
{
	if (!Binding.Ability.AbilityTag.IsValid() || !ASC) return;

	auto Fn = [ASC, Binding]
	{
		ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(Binding.Ability.AbilityTag));
	};

	FTimerHandle& Handle = AbilityTimers.FindOrAdd(TriggeredTag);
	ExecuteDelayed(Fn, Binding.Ability.DelaySeconds, Handle);
}

void USyncCombatComponent::QueueEffectRemove(const FTagReactionBinding& Binding, const FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC)
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

FName USyncCombatComponent::GetRemoveTimerKey(const FTagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const
{
	return Binding.Effects.RemoveTimerKey.IsNone()
		? TriggeredTag.GetTagName()
		: Binding.Effects.RemoveTimerKey;
}

void USyncCombatComponent::QueueEffectApply(const FTagReactionBinding& Binding, const FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC)
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

template <typename Func>
void USyncCombatComponent::ExecuteDelayed(Func InFunction, float DelaySeconds, FTimerHandle& TimerHandle)
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
