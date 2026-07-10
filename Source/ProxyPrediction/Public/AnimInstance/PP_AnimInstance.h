#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "AbilityMotion/Types/PP_AnimInstanceTypes.h"
#include "PP_AnimInstance.generated.h"

class ACharacter;
class UAbilitySystemComponent;
class UAnimMontage;
class UCharacterMovementComponent;
class UPP_AbilityMotionComponent;
class UPP_GameplayAbility;

UCLASS()
class PROXYPREDICTION_API UPP_AnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	// Anim instance lifecycle.
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

	/** Mirrors the replicated ability motion state onto this project anim instance. */
	void ApplyAbilityMotionState(const FPP_AbilityMotionState& NewState);

	/** Resolves the currently animating project ability and returns its montage percent. */
	bool GetAbilityPercentMontagePlayed(float& OutPercent, UPP_GameplayAbility*& OutAbility);

	/** Cached ASC accessor used by the anim instance update path. */
	UAbilitySystemComponent* GetAbilitySystemComponentSafe();

	/** Cached ability motion component accessor used by the anim instance update path. */
	UPP_AbilityMotionComponent* GetMotionComponentSafe();

	/** Applies local correction-ignore flags for abilities that need extra prediction smoothing. */
	void ApplyAbilityMovementCorrectionOverride(const UPP_GameplayAbility* Ability);

	/** Restores local movement correction flags after the ability ends or changes. */
	void RestoreAbilityMovementCorrectionOverride();

	UPROPERTY()
	TObjectPtr<ACharacter> Character = nullptr;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UPP_AbilityMotionComponent> MotionComponent = nullptr;

	/** Ability instance that owned the previous anim update. Used to detect ability changes. */
	UPROPERTY()
	TObjectPtr<const UPP_GameplayAbility> LastTrackedAbility = nullptr;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsBlocking = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsKnockdown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsFlinching = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsFrozen = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsStunned = false;
};

