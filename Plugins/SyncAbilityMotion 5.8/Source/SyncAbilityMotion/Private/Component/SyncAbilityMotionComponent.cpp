#include "Component/SyncAbilityMotionComponent.h"

#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

USyncAbilityMotionComponent::USyncAbilityMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USyncAbilityMotionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(USyncAbilityMotionComponent, AbilityMotionState, COND_SkipOwner);
}

void USyncAbilityMotionComponent::SetAbilityMotionState(const FSyncAbilityMotionState& NewState)
{
	if (AbilityMotionState == NewState) return;

	AbilityMotionState = NewState;
	ApplyAbilityMotionState(NewState);

	ACharacter* Character = GetOwnerCharacter();
	if (Character && !Character->HasAuthority())
	{
		ServerSetAbilityMotionState(NewState);
	}
}

void USyncAbilityMotionComponent::ResetAbilityMotionState()
{
	if (ACharacter* Character = GetOwnerCharacter())
	{
		if (USyncAbilityMotionCharacterMovementComponent* MoveComp =
			Cast<USyncAbilityMotionCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			MoveComp->SetAbilityRootMotionSuppressed(false);
			MoveComp->SetAbilityMovementInputSuppressed(false);
		}
	}

	FSyncAbilityMotionState DefaultState;
	DefaultState.bCanBlendMontage = false;
	DefaultState.bShouldBlendLowerBody = false;
	DefaultState.bRootMotionEnabled = true;
	DefaultState.bMovementInputSuppressed = false;

	SetAbilityMotionState(DefaultState);
}

void USyncAbilityMotionComponent::ServerSetAbilityMotionState_Implementation(const FSyncAbilityMotionState& NewState)
{
	if (AbilityMotionState == NewState) return;

	AbilityMotionState = NewState;
	ApplyAbilityMotionState(NewState);
}

void USyncAbilityMotionComponent::OnRep_AbilityMotionState()
{
	const ACharacter* Character = GetOwnerCharacter();
	if (Character && Character->IsLocallyControlled())
	{
		return;
	}

	ApplyAbilityMotionState(AbilityMotionState);
}

void USyncAbilityMotionComponent::ApplyAbilityMotionState(const FSyncAbilityMotionState& NewState)
{
	ACharacter* Character = GetOwnerCharacter();
	if (!Character) return;

	USkeletalMeshComponent* MeshComp = Character->GetMesh();
	if (!MeshComp) return;

	USyncAbilityMotionAnimInstance* AnimInstance =
		Cast<USyncAbilityMotionAnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	AnimInstance->bCanBlendMontage = NewState.bCanBlendMontage;
	AnimInstance->bShouldBlendLowerBody = NewState.bShouldBlendLowerBody;

	USyncAbilityMotionCharacterMovementComponent* MoveComp =
		Cast<USyncAbilityMotionCharacterMovementComponent>(Character->GetCharacterMovement());
	const bool bUseMovementComponent =
		MoveComp && (Character->IsLocallyControlled() || (Character->HasAuthority() && !Character->IsPlayerControlled()));

	if (bUseMovementComponent)
	{
		MoveComp->SetAbilityRootMotionSuppressed(!NewState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(NewState.bMovementInputSuppressed);
		MoveComp->RefreshAbilityRootMotionMode();
		return;
	}

	if (Character->HasAuthority())
	{
		return;
	}

	AnimInstance->bRootMotionEnabled = NewState.bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(NewState.bRootMotionEnabled
		? ERootMotionMode::RootMotionFromMontagesOnly
		: ERootMotionMode::IgnoreRootMotion);
}

ACharacter* USyncAbilityMotionComponent::GetOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}
