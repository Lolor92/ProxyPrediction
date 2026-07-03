#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Ability/SyncAbilityMotionGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimMontage.h"
#include "Component/SyncAbilityMotionComponent.h"
#include "Data/SyncAbilityMotionTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"

void USyncAbilityMotionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	APawn* PawnOwner = TryGetPawnOwner();
	if (!PawnOwner) return;

	Character = Cast<ACharacter>(PawnOwner);
	if (!Character) return;

	CharacterMovementComponent = Character->GetCharacterMovement();
	AbilitySystemComponent = GetAbilitySystemComponentSafe();
	MotionComponent = GetMotionComponentSafe();
}

void USyncAbilityMotionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Character || !CharacterMovementComponent) return;

	bIsAccelerating = CharacterMovementComponent->GetCurrentAcceleration().Size() > 0.f;
	GroundSpeed = UKismetMathLibrary::VSizeXY(CharacterMovementComponent->Velocity);
	IsAirBorne = CharacterMovementComponent->IsFalling();

	AimRotation = Character->GetBaseAimRotation();

	if (!Character->GetVelocity().IsNearlyZero())
	{
		MovementRotation = UKismetMathLibrary::MakeRotFromX(Character->GetVelocity());
		MovementOffsetYaw = UKismetMathLibrary::NormalizedDeltaRotator(MovementRotation, AimRotation).Yaw;
	}
	else
	{
		MovementOffsetYaw = 0.f;
	}

	UpdateAbilityMotionReplication();

	float Percent = 0.f;
	USyncAbilityMotionGameplayAbility* Ability = nullptr;
	const bool bHasAbilityContext = GetAbilityPercentMontagePlayed(Percent, Ability);

	const bool bAbilityOwnsRotation = bHasAbilityContext && bRootMotionEnabled && !bShouldBlendLowerBody;
	const bool bAllowControllerYaw = !bAbilityOwnsRotation && bIsAccelerating;

	if (bDriveControllerYawFromAbilityState)
	{
		Character->bUseControllerRotationYaw = bAllowControllerYaw;
		bUseControllerRotationYaw = bAllowControllerYaw;
	}
}

void USyncAbilityMotionAnimInstance::UpdateAbilityMotionReplication()
{
	if (!Character || !Character->IsLocallyControlled()) return;

	USyncAbilityMotionComponent* SyncMotion = GetMotionComponentSafe();
	if (!SyncMotion) return;

	float Percent = 0.f;
	USyncAbilityMotionGameplayAbility* Ability = nullptr;
	const bool bHasAbilityContext = GetAbilityPercentMontagePlayed(Percent, Ability);

	if (!bHasAbilityContext || !Ability)
	{
		RestoreAbilityMovementCorrectionOverride();
		LastTrackedAbility = nullptr;
		LastTrackedAbilityActivationSequenceId = 0;
		LastTrackedMontage = nullptr;
		bReleasedRootMotionThisMontage = false;
		SyncMotion->ClearRootMotionCollisionProbe();
		SyncMotion->ResetAbilityMotionState();
		return;
	}

	const UAnimMontage* CurrentMontage = Ability->GetCurrentMontage();
	const uint32 CurrentActivationSequenceId = Ability->GetActivationSequenceId();

	if (Ability != LastTrackedAbility
		|| CurrentActivationSequenceId != LastTrackedAbilityActivationSequenceId
		|| CurrentMontage != LastTrackedMontage)
	{
		RestoreAbilityMovementCorrectionOverride();

		UE_LOG(LogTemp, Warning,
			TEXT("SAM_COLLISION MONTAGE_TRACK Owner=%s Ability=%s Montage=%s Seq=%u ProbeDist=%.1f FallbackDist=%.1f Angle=%.1f IgnoreCorrections=%d PrevRootMotion=%d PrevIgnoreError=%d"),
			*GetNameSafe(Character),
			*GetNameSafe(Ability),
			*GetNameSafe(CurrentMontage),
			CurrentActivationSequenceId,
			Ability->GetRootMotionCharacterCollisionProbeDistance(),
			Ability->GetRootMotionCharacterCollisionFallbackProbeDistance(),
			Ability->GetRootMotionCharacterCollisionForwardAngleDegrees(),
			Ability->ShouldIgnoreMovementCorrectionsDuringAbility() ? 1 : 0,
			SyncMotion->GetAbilityMotionState().bRootMotionEnabled ? 1 : 0,
			CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0);

		SyncMotion->ClearRootMotionCollisionProbe();

		LastTrackedAbility = Ability;
		LastTrackedAbilityActivationSequenceId = CurrentActivationSequenceId;
		LastTrackedMontage = CurrentMontage;
		bReleasedRootMotionThisMontage = false;
	}

	ApplyAbilityMovementCorrectionOverride(Ability);

	const bool bReachedReleasePoint =
		!Ability->MontageLockout.bUseMontageProgressLockout ||
		Percent >= Ability->MontageLockout.MontageProgressBeforeInterrupt;

	const bool bHasMovementInput =
		CharacterMovementComponent &&
		!CharacterMovementComponent->GetCurrentAcceleration().IsNearlyZero(0.01f);

	if (bReachedReleasePoint && bHasMovementInput)
	{
		if (!bReleasedRootMotionThisMontage)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SAM_COLLISION ROOT_RELEASED_BY_INPUT Owner=%s Percent=%.1f UnlockPercent=%.1f"),
				*GetNameSafe(Character),
				Percent,
				Ability->MontageLockout.MontageProgressBeforeInterrupt);
		}

		bReleasedRootMotionThisMontage = true;
	}

	const bool bShouldWatchCharacterCollision =
		!bReleasedRootMotionThisMontage &&
		Ability->IsRootMotionCharacterCollisionPauseEnabled();

	SyncMotion->ConfigureRootMotionCollisionProbe(
		bShouldWatchCharacterCollision,
		Ability->GetRootMotionCharacterCollisionProbeDistance(),
		Ability->GetRootMotionCharacterCollisionForwardAngleDegrees(),
		Ability->GetRootMotionCharacterCollisionFallbackProbeDistance());

	const bool bPausedByCharacterCollision =
		bShouldWatchCharacterCollision &&
		SyncMotion->HasRootMotionBlockingCharacterCollision();

	if (!bShouldWatchCharacterCollision)
	{
		SyncMotion->ClearRootMotionCollisionProbe();
	}

	FSyncAbilityMotionState DesiredState;
	DesiredState.bCanBlendMontage = bReachedReleasePoint;
	DesiredState.bShouldBlendLowerBody = bReachedReleasePoint && bHasMovementInput;
	DesiredState.bRootMotionEnabled = !bReleasedRootMotionThisMontage && !bPausedByCharacterCollision;
	DesiredState.bMovementInputSuppressed = !bReachedReleasePoint;

	if (USyncAbilityMotionCharacterMovementComponent* MoveComp =
		Cast<USyncAbilityMotionCharacterMovementComponent>(CharacterMovementComponent))
	{
		MoveComp->SetAbilityRootMotionSuppressed(!DesiredState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(DesiredState.bMovementInputSuppressed);
	}

	if (SyncMotion->GetAbilityMotionState() == DesiredState) return;

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_COLLISION STATE_CHANGE Owner=%s Percent=%.1f RootMotion=%d PausedByCollision=%d Released=%d WatchCollision=%d ReachedRelease=%d HasInput=%d BlendMontage=%d LowerBody=%d SuppressInput=%d IgnoreError=%d"),
		*GetNameSafe(Character),
		Percent,
		DesiredState.bRootMotionEnabled ? 1 : 0,
		bPausedByCharacterCollision ? 1 : 0,
		bReleasedRootMotionThisMontage ? 1 : 0,
		bShouldWatchCharacterCollision ? 1 : 0,
		bReachedReleasePoint ? 1 : 0,
		bHasMovementInput ? 1 : 0,
		DesiredState.bCanBlendMontage ? 1 : 0,
		DesiredState.bShouldBlendLowerBody ? 1 : 0,
		DesiredState.bMovementInputSuppressed ? 1 : 0,
		CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0);

	SyncMotion->SetAbilityMotionState(DesiredState);
}

bool USyncAbilityMotionAnimInstance::GetAbilityPercentMontagePlayed(
	float& OutPercent,
	USyncAbilityMotionGameplayAbility*& OutAbility)
{
	OutPercent = 0.f;
	OutAbility = nullptr;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentSafe();
	if (!ASC) return false;

	UGameplayAbility* ActiveAbility = ASC->GetAnimatingAbility();
	if (!ActiveAbility) return false;

	UAnimMontage* CurrentMontage = ActiveAbility->GetCurrentMontage();
	if (!CurrentMontage) return false;

	USyncAbilityMotionGameplayAbility* Ability = Cast<USyncAbilityMotionGameplayAbility>(ActiveAbility);
	if (!Ability) return false;

	const float MontageLength = CurrentMontage->GetPlayLength();
	if (MontageLength <= 0.f) return false;

	const float CurrentPosition = Montage_GetPosition(CurrentMontage);
	OutPercent = (CurrentPosition / MontageLength) * 100.f;
	OutAbility = Ability;

	return true;
}

UAbilitySystemComponent* USyncAbilityMotionAnimInstance::GetAbilitySystemComponentSafe()
{
	if (!AbilitySystemComponent && Character)
	{
		if (IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(Character))
		{
			AbilitySystemComponent = AbilityOwner->GetAbilitySystemComponent();
		}

		if (!AbilitySystemComponent)
		{
			AbilitySystemComponent = Character->FindComponentByClass<UAbilitySystemComponent>();
		}
	}

	return AbilitySystemComponent;
}

USyncAbilityMotionComponent* USyncAbilityMotionAnimInstance::GetMotionComponentSafe()
{
	if (!MotionComponent && Character)
	{
		MotionComponent = Character->FindComponentByClass<USyncAbilityMotionComponent>();
	}

	return MotionComponent;
}

void USyncAbilityMotionAnimInstance::ApplyAbilityMovementCorrectionOverride(const USyncAbilityMotionGameplayAbility* Ability)
{
if (!CharacterMovementComponent || !Ability || !Ability->ShouldIgnoreMovementCorrectionsDuringAbility())
{
return;
}

if (!bHasSavedMovementCorrectionFlags)
{
bSavedIgnoreClientMovementErrorChecksAndCorrection = CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection;
bHasSavedMovementCorrectionFlags = true;

UE_LOG(LogTemp, Warning,
TEXT("SAM_COLLISION CORRECTION_IGNORE_BEGIN Owner=%s SavedIgnoreError=%d"),
*GetNameSafe(Character),
bSavedIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0);
}

CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = true;
}

void USyncAbilityMotionAnimInstance::RestoreAbilityMovementCorrectionOverride()
{
if (!CharacterMovementComponent || !bHasSavedMovementCorrectionFlags)
{
return;
}

CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bSavedIgnoreClientMovementErrorChecksAndCorrection;

UE_LOG(LogTemp, Warning,
TEXT("SAM_COLLISION CORRECTION_IGNORE_END Owner=%s RestoredIgnoreError=%d"),
*GetNameSafe(Character),
bSavedIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0);

bHasSavedMovementCorrectionFlags = false;
}
