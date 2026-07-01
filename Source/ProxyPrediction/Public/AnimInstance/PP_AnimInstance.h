#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PP_AnimInstance.generated.h"

class ACharacter;
class UAbilitySystemComponent;
class UCharacterMovementComponent;

UCLASS()
class PROXYPREDICTION_API UPP_AnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	// Anim instance lifecycle.
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsBlocking = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsKnockdown = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsFlinching = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsFrozen = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsStunned = false;
	
protected:
	UPROPERTY()
	TObjectPtr<ACharacter> Character = nullptr;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;
	
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
