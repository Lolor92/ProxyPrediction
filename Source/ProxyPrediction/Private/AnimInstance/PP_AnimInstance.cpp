#include "ProxyPrediction/Public/AnimInstance/PP_AnimInstance.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimMontage.h"
#include "AbilityMotion/Component/PP_AbilityMotionComponent.h"
#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"
#include "GAS/Ability/PP_GameplayAbility.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

void UPP_AnimInstance::NativeInitializeAnimation()
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

void UPP_AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Character || !CharacterMovementComponent) return;

	bIsAccelerating = CharacterMovementComponent->GetCurrentAcceleration().Size() > 0.f;
	GroundSpeed = UKismetMathLibrary::VSizeXY(CharacterMovementComponent->Velocity);
	IsAirBorne = CharacterMovementComponent->IsFalling();

	AimRotation = Character->GetBaseAimRotation();

	const FVector Velocity = Character->GetVelocity();
	if (!Velocity.IsNearlyZero())
	{
		MovementRotation = UKismetMathLibrary::MakeRotFromX(Velocity);
		MovementOffsetYaw = UKismetMathLibrary::NormalizedDeltaRotator(MovementRotation, AimRotation).Yaw;
	}
	else
	{
		MovementOffsetYaw = 0.f;
	}

	UpdateAbilityMotionReplication();

	if (UPP_AbilityMotionComponent* Motion = GetMotionComponentSafe())
	{
		ApplyAbilityMotionState(Motion->GetAbilityMotionState());
	}

	float Percent = 0.f;
	UPP_GameplayAbility* Ability = nullptr;
	const bool bHasAbilityContext = GetAbilityPercentMontagePlayed(Percent, Ability);

	const bool bAbilityOwnsRotation = bHasAbilityContext && bRootMotionEnabled && !bShouldBlendLowerBody;
	const bool bAllowControllerYaw = !bAbilityOwnsRotation && bIsAccelerating;

	if (bDriveControllerYawFromAbilityState)
	{
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
	else
	{
		Character->bUseControllerRotationYaw = bIsAccelerating;
		bUseControllerRotationYaw = Character->bUseControllerRotationYaw;
	}
}

void UPP_AnimInstance::UpdateAbilityMotionReplication()
{
	if (!Character || !Character->IsLocallyControlled()) return;

	UPP_AbilityMotionComponent* Motion = GetMotionComponentSafe();
	if (!Motion) return;

	float Percent = 0.f;
	UPP_GameplayAbility* Ability = nullptr;
	const bool bHasAbilityContext = GetAbilityPercentMontagePlayed(Percent, Ability);

	if (!bHasAbilityContext || !Ability)
	{
		Motion->SetServerMovementCorrectionIgnoreForAbility(false);
		RestoreAbilityMovementCorrectionOverride();
		LastTrackedAbility = nullptr;
		LastTrackedAbilityActivationSequenceId = 0;
		LastTrackedMontage = nullptr;
		bReleasedRootMotionThisMontage = false;
		bRootMotionCollisionPauseHeldUntilRelease = false;
		Motion->ClearRootMotionCollisionProbe();
		Motion->ResetAbilityMotionState();
		return;
	}

	const UAnimMontage* CurrentMontage = Ability->GetCurrentMontage();
	const uint32 CurrentActivationSequenceId = Ability->GetActivationSequenceId();

	if (Ability != LastTrackedAbility
		|| CurrentActivationSequenceId != LastTrackedAbilityActivationSequenceId
		|| CurrentMontage != LastTrackedMontage)
	{
		Motion->SetServerMovementCorrectionIgnoreForAbility(false);
		RestoreAbilityMovementCorrectionOverride();
		Motion->ClearRootMotionCollisionProbe();

		LastTrackedAbility = Ability;
		LastTrackedAbilityActivationSequenceId = CurrentActivationSequenceId;
		LastTrackedMontage = CurrentMontage;
		bReleasedRootMotionThisMontage = false;
		bRootMotionCollisionPauseHeldUntilRelease = false;
	}

	ApplyAbilityMovementCorrectionOverride(Ability);
	Motion->SetServerMovementCorrectionIgnoreForAbility(Ability->ShouldIgnoreMovementCorrectionsDuringAbility());

	const bool bReachedReleasePoint =
		!Ability->MontageLockout.bUseMontageProgressLockout ||
		Percent >= Ability->MontageLockout.MontageProgressBeforeInterrupt;

	const bool bHasMovementInput =
		CharacterMovementComponent &&
		!CharacterMovementComponent->GetCurrentAcceleration().IsNearlyZero(0.01f);

	if (bReachedReleasePoint && bHasMovementInput)
	{
		bReleasedRootMotionThisMontage = true;
	}

	const bool bShouldWatchCharacterCollision =
		!bReleasedRootMotionThisMontage &&
		Ability->IsRootMotionCharacterCollisionPauseEnabled();

	Motion->ConfigureRootMotionCollisionProbe(
		bShouldWatchCharacterCollision,
		Ability->GetRootMotionCharacterCollisionProbeDistance(),
		Ability->GetRootMotionCharacterCollisionForwardAngleDegrees(),
		Ability->GetRootMotionCharacterCollisionFallbackProbeDistance());

	const bool bRawPausedByCharacterCollision =
		bShouldWatchCharacterCollision &&
		Motion->HasRootMotionBlockingCharacterCollision();

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
		Motion->ClearRootMotionCollisionProbe();
	}

	FPP_AbilityMotionState DesiredState;
	DesiredState.bCanBlendMontage = bReachedReleasePoint;
	DesiredState.bShouldBlendLowerBody = bReachedReleasePoint && bHasMovementInput;
	DesiredState.bRootMotionEnabled = !bReleasedRootMotionThisMontage && !bPausedByCharacterCollision;
	DesiredState.bMovementInputSuppressed = !bReachedReleasePoint;

	if (UPP_CharacterMovementComponent* MoveComp =
		Cast<UPP_CharacterMovementComponent>(CharacterMovementComponent))
	{
		MoveComp->SetAbilityRootMotionSuppressed(!DesiredState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(DesiredState.bMovementInputSuppressed);
	}

	if (Motion->GetAbilityMotionState() == DesiredState) return;

	Motion->SetAbilityMotionState(DesiredState);
}

void UPP_AnimInstance::ApplyAbilityMotionState(const FPP_AbilityMotionState& NewState)
{
	bCanBlendMontage = NewState.bCanBlendMontage;
	bShouldBlendLowerBody = NewState.bShouldBlendLowerBody;

	UPP_CharacterMovementComponent* MoveComp =
		Cast<UPP_CharacterMovementComponent>(CharacterMovementComponent);
	const bool bUseMovementComponent =
		MoveComp && Character && (Character->IsLocallyControlled() || (Character->HasAuthority() && !Character->IsPlayerControlled()));

	if (bUseMovementComponent)
	{
		MoveComp->SetAbilityRootMotionSuppressed(!NewState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(NewState.bMovementInputSuppressed);
		const bool bLocalOwnerReaction =
			MoveComp->ShouldIgnoreServerRootMotionMontageTrackCorrection() && Character->IsLocallyControlled();
		const bool bEffectiveRootMotionEnabled = bLocalOwnerReaction || NewState.bRootMotionEnabled;
		bRootMotionEnabled = bEffectiveRootMotionEnabled;
		SetRootMotionMode(bEffectiveRootMotionEnabled
			? ERootMotionMode::RootMotionFromMontagesOnly
			: ERootMotionMode::IgnoreRootMotion);
		return;
	}

	if (Character && Character->HasAuthority())
	{
		return;
	}

	bRootMotionEnabled = NewState.bRootMotionEnabled;
	SetRootMotionMode(NewState.bRootMotionEnabled
		? ERootMotionMode::RootMotionFromMontagesOnly
		: ERootMotionMode::IgnoreRootMotion);
}

bool UPP_AnimInstance::GetAbilityPercentMontagePlayed(
	float& OutPercent,
	UPP_GameplayAbility*& OutAbility)
{
	OutPercent = 0.f;
	OutAbility = nullptr;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentSafe();
	if (!ASC) return false;

	UGameplayAbility* ActiveAbility = ASC->GetAnimatingAbility();
	if (!ActiveAbility) return false;

	UAnimMontage* CurrentMontage = ActiveAbility->GetCurrentMontage();
	if (!CurrentMontage) return false;

	UPP_GameplayAbility* Ability = Cast<UPP_GameplayAbility>(ActiveAbility);
	if (!Ability) return false;

	const float MontageLength = CurrentMontage->GetPlayLength();
	if (MontageLength <= 0.f) return false;

	const float CurrentPosition = Montage_GetPosition(CurrentMontage);
	OutPercent = (CurrentPosition / MontageLength) * 100.f;
	OutAbility = Ability;

	return true;
}

UAbilitySystemComponent* UPP_AnimInstance::GetAbilitySystemComponentSafe()
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

UPP_AbilityMotionComponent* UPP_AnimInstance::GetMotionComponentSafe()
{
	if (!MotionComponent && Character)
	{
		MotionComponent = Character->FindComponentByClass<UPP_AbilityMotionComponent>();
	}

	return MotionComponent;
}

void UPP_AnimInstance::ApplyAbilityMovementCorrectionOverride(const UPP_GameplayAbility* Ability)
{
	if (!CharacterMovementComponent || !Ability || !Ability->ShouldIgnoreMovementCorrectionsDuringAbility())
	{
		return;
	}

	if (!bHasSavedMovementCorrectionFlags)
	{
		bSavedIgnoreClientMovementErrorChecksAndCorrection =
			CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection;
		bHasSavedMovementCorrectionFlags = true;
	}

	CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = true;
}

void UPP_AnimInstance::RestoreAbilityMovementCorrectionOverride()
{
	if (!CharacterMovementComponent || !bHasSavedMovementCorrectionFlags)
	{
		return;
	}

	CharacterMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection =
		bSavedIgnoreClientMovementErrorChecksAndCorrection;

	bHasSavedMovementCorrectionFlags = false;
}

