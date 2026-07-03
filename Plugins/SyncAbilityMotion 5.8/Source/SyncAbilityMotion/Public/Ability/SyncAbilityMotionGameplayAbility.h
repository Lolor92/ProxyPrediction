#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
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

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo")
	TSubclassOf<UGameplayAbility> ComboAbilityClass = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo", meta=(ClampMin="0.0", Units="Seconds"))
	float ComboWindowDuration = 2.f;

	TSubclassOf<UGameplayAbility> GetComboAbilityClass() const { return ComboAbilityClass; }
	float GetComboWindowDuration() const { return ComboWindowDuration; }
	bool IsComboWindowOpen() const { return bComboWindowOpen; }
	uint32 GetActivationSequenceId() const { return ActivationSequenceId; }
	bool IsRootMotionCharacterCollisionPauseEnabled() const { return bPauseRootMotionOnCharacterCollision; }
	float GetRootMotionCharacterCollisionForwardAngleDegrees() const { return RootMotionCharacterCollisionForwardAngleDegrees; }
	float GetRootMotionCharacterCollisionProbeDistance() const { return RootMotionCharacterCollisionProbeDistance; }
	float GetRootMotionCharacterCollisionFallbackProbeDistance() const { return FMath::Max(RootMotionCharacterCollisionProbeDistance, RootMotionCharacterCollisionFallbackProbeDistance); }
	bool ShouldIgnoreMovementCorrectionsDuringAbility() const { return bIgnoreMovementCorrectionsDuringAbility; }
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
	float RootMotionCharacterCollisionProbeDistance = 40.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ClampMin="0.0", UIMin="0.0", Units="cm"))
	float RootMotionCharacterCollisionFallbackProbeDistance = 40.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion")
	bool bIgnoreMovementCorrectionsDuringAbility = false;

	UFUNCTION(BlueprintCallable, Category="Ability|Combo")
	void OpenComboWindow();

	UFUNCTION(BlueprintCallable, Category="Ability|Combo")
	void CloseComboWindow();

private:
	void ResetComboWindow();

	FTimerHandle ComboWindowTimerHandle;

	bool bComboWindowOpen = false;

	uint32 ActivationSequenceId = 0;
};