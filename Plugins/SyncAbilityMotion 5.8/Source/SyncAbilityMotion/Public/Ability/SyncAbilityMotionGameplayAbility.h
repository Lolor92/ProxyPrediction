#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Data/SyncAbilityMotionTypes.h"
#include "SyncAbilityMotionGameplayAbility.generated.h"

class ACharacter;

UCLASS()
class SYNCABILITYMOTION_API USyncAbilityMotionGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	USyncAbilityMotionGameplayAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Montage", meta=(DisplayName="Montage Lockout"))
	FSyncAbilityMotionMontageLockout MontageLockout;

	uint32 GetActivationSequenceId() const { return ActivationSequenceId; }
	bool ShouldPauseRootMotionForCharacterCollision(const ACharacter* Character) const;

protected:
	bool CanInterruptAnimatingAbility(const FGameplayAbilityActorInfo* ActorInfo) const;

	UFUNCTION(BlueprintCallable, Category="Ability|Rotation")
	void RotateAvatarToControllerYawOnActivate() const;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Rotation")
	bool bRotateToControllerYawOnActivate = false;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion")
	bool bPauseRootMotionOnCharacterCollision = true;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="Degrees"))
	float RootMotionCharacterCollisionForwardAngleDegrees = 40.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ClampMin="0.0", UIMin="0.0", Units="cm"))
	float RootMotionCharacterCollisionProbeDistance = 25.f;

private:
	uint32 ActivationSequenceId = 0;
};
