#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SyncAbilityMotionAnimInstance.generated.h"

class ACharacter;
class UAbilitySystemComponent;
class UAnimMontage;
class UCharacterMovementComponent;
class USyncAbilityMotionComponent;
class USyncAbilityMotionGameplayAbility;

UCLASS()
class SYNCABILITYMOTION_API USyncAbilityMotionAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bCanBlendMontage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bShouldBlendLowerBody = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bRootMotionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bDriveControllerYawFromAbilityState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bUseControllerRotationYaw = true;

	/** When true, movement smoothly turns the character toward camera/controller yaw instead of snapping via bUseControllerRotationYaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement|Rotation")
	bool bSmoothFaceCameraYawWhenMoving = true;

	/** Degrees per second. Higher is snappier but still avoids a hard teleport-snap. Try 720-1440. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement|Rotation", meta=(ClampMin="0.0"))
	float CameraFacingYawRotationSpeed = 1080.f;

	/** If yaw error is tiny, snap the final bit to prevent micro-jitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement|Rotation", meta=(ClampMin="0.0"))
	float CameraFacingYawSnapTolerance = 1.0f;

protected:
	/** Builds and publishes the local ability movement state used by the character and simulated proxies. */
	void UpdateAbilityMotionReplication();

	/** Resolves the currently animating SyncAbilityMotion ability and returns its montage percent. */
	bool GetAbilityPercentMontagePlayed(float& OutPercent, USyncAbilityMotionGameplayAbility*& OutAbility);

	/** Cached ASC accessor used by the anim instance update path. */
	UAbilitySystemComponent* GetAbilitySystemComponentSafe();

	/** Cached SyncAbilityMotion component accessor used by the anim instance update path. */
	USyncAbilityMotionComponent* GetMotionComponentSafe();

	/** Applies local correction-ignore flags for abilities that need extra prediction smoothing. */
	void ApplyAbilityMovementCorrectionOverride(const USyncAbilityMotionGameplayAbility* Ability);

	/** Restores local movement correction flags after the ability ends or changes. */
	void RestoreAbilityMovementCorrectionOverride();

	UPROPERTY()
	TObjectPtr<ACharacter> Character = nullptr;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY()
	TObjectPtr<USyncAbilityMotionComponent> MotionComponent = nullptr;

	/** Ability instance that owned the previous anim update. Used to detect ability changes. */
	UPROPERTY()
	TObjectPtr<const USyncAbilityMotionGameplayAbility> LastTrackedAbility = nullptr;

	/** Activation sequence for the tracked ability. Prevents stale state when the same ability class is reused. */
	UPROPERTY()
	uint32 LastTrackedAbilityActivationSequenceId = 0;

	/** Montage that owned the previous anim update. Used to reset per-montage movement state. */
	UPROPERTY()
	TObjectPtr<const UAnimMontage> LastTrackedMontage = nullptr;

	/** True after player movement input releases root motion at the montage lockout point. */
	UPROPERTY()
	bool bReleasedRootMotionThisMontage = false;

	/** Per-montage latch used by fast abilities that should not resume root motion until their release point after a collision pause. */
	bool bRootMotionCollisionPauseHeldUntilRelease = false;

	bool bHasSavedMovementCorrectionFlags = false;
	bool bSavedIgnoreClientMovementErrorChecksAndCorrection = false;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	float GroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	bool bIsAccelerating = false;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	bool IsAirBorne = false;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	FRotator AimRotation;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	FRotator MovementRotation;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	float MovementOffsetYaw = 0.f;
};
