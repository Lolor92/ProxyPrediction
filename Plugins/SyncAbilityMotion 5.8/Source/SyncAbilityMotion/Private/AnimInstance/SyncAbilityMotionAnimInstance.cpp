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
		// Do not enable bUseControllerRotationYaw while moving.
		// That makes ACharacter copy controller yaw directly, which causes the visible snap
		// when the camera is already rotated away from the character.
		const bool bShouldSmoothFaceCameraYaw = bAllowControllerYaw && bSmoothFaceCameraYawWhenMoving;

		Character->bUseControllerRotationYaw = bAllowControllerYaw && !bShouldSmoothFaceCameraYaw;
		bUseControllerRotationYaw = Character->bUseControllerRotationYaw;

		if (bShouldSmoothFaceCameraYaw && DeltaSeconds > 0.f)
		{
			FRotator CurrentRotation = Character->GetActorRotation();
			FRotator TargetRotation = CurrentRotation;
			TargetRotation.Yaw = AimRotation.Yaw;

			const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetRotation.Yaw));
			if (YawDelta <= CameraFacingYawSnapTolerance)
			{
				Character->SetActorRotation(TargetRotation);
			}
			else
			{
				const FRotator NewRotation = FMath::RInterpConstantTo(
					CurrentRotation,
					TargetRotation,
					DeltaSeconds,
					CameraFacingYawRotationSpeed);

				Character->SetActorRotation(NewRotation);
			}
		}
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
		SyncMotion->SetServerMovementCorrectionIgnoreForAbility(false);
		RestoreAbilityMovementCorrectionOverride();
		LastTrackedAbility = nullptr;
		LastTrackedAbilityActivationSequenceId = 0;
		LastTrackedMontage = nullptr;
		bReleasedRootMotionThisMontage = false;
		bRootMotionCollisionPauseHeldUntilRelease = false;
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
		SyncMotion->SetServerMovementCorrectionIgnoreForAbility(false);
		RestoreAbilityMovementCorrectionOverride();

		
		SyncMotion->ClearRootMotionCollisionProbe();

		LastTrackedAbility = Ability;
		LastTrackedAbilityActivationSequenceId = CurrentActivationSequenceId;
		LastTrackedMontage = CurrentMontage;
		bReleasedRootMotionThisMontage = false;
		bRootMotionCollisionPauseHeldUntilRelease = false;
	}

	ApplyAbilityMovementCorrectionOverride(Ability);
	SyncMotion->SetServerMovementCorrectionIgnoreForAbility(Ability->ShouldIgnoreMovementCorrectionsDuringAbility());

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

	const bool bRawPausedByCharacterCollision =
		bShouldWatchCharacterCollision &&
		SyncMotion->HasRootMotionBlockingCharacterCollision();

	if (Ability->ShouldHoldRootMotionCollisionPauseUntilRelease())
	{
		if (bRawPausedByCharacterCollision && !bReachedReleasePoint)
		{
			bRootMotionCollisionPauseHeldUntilRelease = true;
		}

		if (bReachedReleasePoint)
		{
			bRootMotionCollisionPauseHeldUntilRelease = false;
		}
	}
	else
	{
		bRootMotionCollisionPauseHeldUntilRelease = false;
	}

	const bool bPausedByCharacterCollision =
		Ability->ShouldHoldRootMotionCollisionPauseUntilRelease()
			? (!bReachedReleasePoint && (bRawPausedByCharacterCollision || bRootMotionCollisionPauseHeldUntilRelease))
			: bRawPausedByCharacterCollision;

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


bHasSavedMovementCorrectionFlags = false;
}
