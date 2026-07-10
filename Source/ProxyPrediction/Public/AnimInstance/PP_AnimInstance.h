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

/**
 * Builds locomotion values and translates active ability settings into movement state.
 * The local owner publishes that state so the server and proxies animate consistently.
 */
UCLASS()
class PROXYPREDICTION_API UPP_AnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	// Anim instance lifecycle.
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** True after the active montage reaches its configured blend-unlock point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bCanBlendMontage = false;

	/** True when locomotion should drive the lower body under the montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bShouldBlendLowerBody = false;

	/** True when the active montage may move the character with root motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bRootMotionEnabled = true;

	/** Lets ability state decide when controller yaw may rotate the character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bDriveControllerYawFromAbilityState = false;

	/** Current controller-yaw state exposed to the Animation Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bUseControllerRotationYaw = true;

	/** Smoothly turns a moving character toward the controller's yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement|Rotation")
	bool bSmoothFaceCameraYawWhenMoving = true;

	/** Maximum camera-facing turn speed in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement|Rotation", meta=(ClampMin="0.0"))
	float CameraFacingYawRotationSpeed = 1080.f;

	/** Yaw error small enough to finish the turn without micro-jitter. */
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

	/** Character currently using this Animation Instance. */
	UPROPERTY()
	TObjectPtr<ACharacter> Character = nullptr;

	/** Cached movement component used to build locomotion values. */
	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent = nullptr;

	/** Cached GAS component used to find the active project ability. */
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	/** Cached component that replicates the resolved ability movement state. */
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
	
	/** Horizontal character speed used by locomotion blends. */
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	float GroundSpeed = 0.f;

	/** True while Character Movement has non-zero acceleration. */
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	bool bIsAccelerating = false;

	/** True while the character is falling or otherwise airborne. */
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	bool IsAirBorne = false;

	/** Character aim relative to its current actor rotation. */
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	FRotator AimRotation;

	/** Rotation of the current horizontal movement direction. */
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	FRotator MovementRotation;

	/** Signed yaw difference between movement direction and aim direction. */
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	float MovementOffsetYaw = 0.f;

	/** True while the character is in a blocking state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsBlocking = false;

	/** True while the character is knocked down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsKnockdown = false;

	/** True while the character is playing a flinch response. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsFlinching = false;

	/** True while the character is frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsFrozen = false;

	/** True while the character is stunned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State")
	bool bIsStunned = false;
};

