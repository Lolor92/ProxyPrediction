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

protected:
	void UpdateAbilityMotionReplication();
	bool GetAbilityPercentMontagePlayed(float& OutPercent, USyncAbilityMotionGameplayAbility*& OutAbility);
	UAbilitySystemComponent* GetAbilitySystemComponentSafe();
	USyncAbilityMotionComponent* GetMotionComponentSafe();

	UPROPERTY()
	TObjectPtr<ACharacter> Character = nullptr;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY()
	TObjectPtr<USyncAbilityMotionComponent> MotionComponent = nullptr;

	UPROPERTY()
	TObjectPtr<const USyncAbilityMotionGameplayAbility> LastTrackedAbility = nullptr;

	UPROPERTY()
	uint32 LastTrackedAbilityActivationSequenceId = 0;

	UPROPERTY()
	TObjectPtr<const UAnimMontage> LastTrackedMontage = nullptr;

	UPROPERTY()
	bool bReleasedRootMotionThisMontage = false;

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
