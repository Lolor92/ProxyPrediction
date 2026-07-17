#include "ProxyPrediction/Public/GAS/Component/PP_AbilitySystemComponent.h"

#include "GAS/Ability/PP_GameplayAbility.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"

namespace
{
	constexpr double PreparedActivationYawLifetimeSeconds = 1.0;
}

UPP_AbilitySystemComponent::UPP_AbilitySystemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPP_AbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();
	GrantAbilitySets();
}

void UPP_AbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveGrantedAbilitySets();
	Super::EndPlay(EndPlayReason);
}

void UPP_AbilitySystemComponent::GrantAbilitySets()
{
	if (bAbilitySetsGranted) return;
	if (!IsOwnerActorAuthoritative()) return;
	
	for (const UPP_AbilitySet* AbilitySet : AbilitySetsToGrant)
	{
		if (!AbilitySet) continue;
		
		AbilitySet->GiveToAbilitySystem(this, &GrantedHandles, GetOwner());
	}
	
	bAbilitySetsGranted = true;
}

void UPP_AbilitySystemComponent::RemoveGrantedAbilitySets()
{
	if (!bAbilitySetsGranted) return;
	if (!IsOwnerActorAuthoritative()) return;
	
	GrantedHandles.ClearAbilities(this);
}

bool UPP_AbilitySystemComponent::TryActivateAbilityWithSyncedFacing(
	const FGameplayAbilitySpecHandle AbilityHandle,
	const bool bAllowRemoteActivation)
{
	const bool bPreparedYaw = PrepareAbilityActivationYaw(AbilityHandle);
	const bool bActivated = TryActivateAbility(AbilityHandle, bAllowRemoteActivation);
	if (!bActivated && bPreparedYaw)
	{
		DiscardPreparedAbilityActivationYaw(AbilityHandle, true);
	}

	return bActivated;
}

bool UPP_AbilitySystemComponent::TryActivateAbilitiesByTagWithSyncedFacing(
	const FGameplayTagContainer& GameplayTagContainer,
	const bool bAllowRemoteActivation)
{
	TArray<FGameplayAbilitySpec*> MatchingSpecPointers;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(GameplayTagContainer, MatchingSpecPointers);
	if (MatchingSpecPointers.IsEmpty())
	{
		return false;
	}

	// Activating can reallocate the internal spec array, so retain handles rather than pointers.
	TArray<FGameplayAbilitySpecHandle> MatchingHandles;
	MatchingHandles.Reserve(MatchingSpecPointers.Num());
	for (const FGameplayAbilitySpec* Spec : MatchingSpecPointers)
	{
		if (Spec)
		{
			MatchingHandles.Add(Spec->Handle);
		}
	}

	bool bActivatedAny = false;
	for (const FGameplayAbilitySpecHandle Handle : MatchingHandles)
	{
		bActivatedAny |= TryActivateAbilityWithSyncedFacing(Handle, bAllowRemoteActivation);
	}

	return bActivatedAny;
}

bool UPP_AbilitySystemComponent::PrepareAbilityActivationYaw(
	const FGameplayAbilitySpecHandle AbilityHandle)
{
	const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(AbilityHandle);
	const UPP_GameplayAbility* Ability = Spec ? Cast<UPP_GameplayAbility>(Spec->Ability) : nullptr;
	const FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	if (!Ability || !Ability->ShouldSynchronizeActivationYaw() || !ActorInfo ||
		!ActorInfo->IsLocallyControlled())
	{
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	AController* Controller = ActorInfo->PlayerController.IsValid()
		? ActorInfo->PlayerController.Get()
		: Character ? Character->GetController() : nullptr;
	UWorld* World = GetWorld();
	if (!Character || !Controller || !World)
	{
		return false;
	}

	const float ActivationYaw = FRotator::NormalizeAxis(Controller->GetControlRotation().Yaw);
	FPendingAbilityActivationYaw& Pending = PendingAbilityActivationYaws.FindOrAdd(AbilityHandle);
	Pending.Yaw = ActivationYaw;
	Pending.PreparedWorldTime = World->GetTimeSeconds();

	if (!IsOwnerActorAuthoritative())
	{
		// This reliable RPC is issued before GAS sends ServerTryActivateAbility on the same ASC.
		ServerPrepareAbilityActivationYaw(AbilityHandle, ActivationYaw);
	}


	return true;
}

void UPP_AbilitySystemComponent::ServerPrepareAbilityActivationYaw_Implementation(
	const FGameplayAbilitySpecHandle AbilityHandle,
	const float ActivationYaw)
{
	const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(AbilityHandle);
	const UPP_GameplayAbility* Ability = Spec ? Cast<UPP_GameplayAbility>(Spec->Ability) : nullptr;
	UWorld* World = GetWorld();
	if (!Ability || !Ability->ShouldSynchronizeActivationYaw() || !World || !FMath::IsFinite(ActivationYaw))
	{
		return;
	}

	FPendingAbilityActivationYaw& Pending = PendingAbilityActivationYaws.FindOrAdd(AbilityHandle);
	Pending.Yaw = FRotator::NormalizeAxis(ActivationYaw);
	Pending.PreparedWorldTime = World->GetTimeSeconds();


}

bool UPP_AbilitySystemComponent::ConsumePreparedAbilityActivationYaw(
	const FGameplayAbilitySpecHandle AbilityHandle,
	float& OutYaw)
{
	FPendingAbilityActivationYaw Pending;
	const FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	const bool bRemoteServerAvatar = IsOwnerActorAuthoritative() && ActorInfo &&
		!ActorInfo->IsLocallyControlled();
	TMap<FGameplayAbilitySpecHandle, FPendingAbilityActivationYaw>& SourceYaws = bRemoteServerAvatar
		? ServerActivationYaws
		: PendingAbilityActivationYaws;
	if (!SourceYaws.RemoveAndCopyValue(AbilityHandle, Pending))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const double Age = World ? World->GetTimeSeconds() - Pending.PreparedWorldTime : TNumericLimits<double>::Max();
	if (Age < 0.0 || Age > PreparedActivationYawLifetimeSeconds || !FMath::IsFinite(Pending.Yaw))
	{

		return false;
	}

	OutYaw = Pending.Yaw;
	return true;
}

void UPP_AbilitySystemComponent::InternalServerTryActivateAbility(
	const FGameplayAbilitySpecHandle AbilityToActivate,
	const bool bInputPressed,
	const FPredictionKey& PredictionKey,
	const FGameplayEventData* TriggerEventData)
{
	// Only the matching GAS activation RPC may consume a yaw delivered by the owner. This keeps a
	// failed predicted activation (or an unrelated authority-side timer activation) from reusing it.
	FPendingAbilityActivationYaw Pending;
	if (PendingAbilityActivationYaws.RemoveAndCopyValue(AbilityToActivate, Pending))
	{
		ServerActivationYaws.Add(AbilityToActivate, Pending);
	}

	Super::InternalServerTryActivateAbility(
		AbilityToActivate, bInputPressed, PredictionKey, TriggerEventData);
	ServerActivationYaws.Remove(AbilityToActivate);
}

void UPP_AbilitySystemComponent::DiscardPreparedAbilityActivationYaw(
	const FGameplayAbilitySpecHandle AbilityHandle,
	const bool bNotifyServer)
{
	PendingAbilityActivationYaws.Remove(AbilityHandle);
	if (bNotifyServer && !IsOwnerActorAuthoritative())
	{
		ServerDiscardPreparedAbilityActivationYaw(AbilityHandle);
	}
}

void UPP_AbilitySystemComponent::ServerDiscardPreparedAbilityActivationYaw_Implementation(
	const FGameplayAbilitySpecHandle AbilityHandle)
{
	PendingAbilityActivationYaws.Remove(AbilityHandle);
}

