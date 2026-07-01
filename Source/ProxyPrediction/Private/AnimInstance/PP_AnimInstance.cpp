#include "ProxyPrediction/Public/AnimInstance/PP_AnimInstance.h"
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
}

void UPP_AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	
	if (!Character || !CharacterMovementComponent) return;

	/* ---------- Movement ---------- */
	bIsAccelerating = CharacterMovementComponent->GetCurrentAcceleration().Size() > 0.f;
	GroundSpeed     = UKismetMathLibrary::VSizeXY(CharacterMovementComponent->Velocity);
	IsAirBorne      = CharacterMovementComponent->IsFalling();
	
	/* ---------- Rotation ---------- */
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
	
	Character->bUseControllerRotationYaw = bIsAccelerating;
}
