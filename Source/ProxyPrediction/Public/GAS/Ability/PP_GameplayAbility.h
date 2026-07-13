#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ActiveGameplayEffectHandle.h"
#include "GAS/Ability/PP_GameplayAbilityTypes.h"
#include "TimerManager.h"
#include "PP_GameplayAbility.generated.h"

class ACharacter;

/** Runtime state for one configured montage effect window. */
struct FPP_AbilityMontageEffectWindowRuntime
{
	FActiveGameplayEffectHandle EffectHandle;
	FTimerHandle ApplyTimer;
	FTimerHandle RemoveTimer;
};

UCLASS()
class PROXYPREDICTION_API UPP_GameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UPP_GameplayAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Montage", meta=(DisplayName="Montage Lockout"))
	FPP_AbilityMotionMontageLockout MontageLockout;

	/** Effects applied once at activation and removed on every ability end/cancel path. */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Effects", meta=(TitleProperty="GameplayEffectClass"))
	TArray<FPP_AbilityOwnedEffect> AbilityLifetimeEffects;

	/** Effects applied and removed once at configured percentages of the active montage. */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Effects", meta=(TitleProperty="GameplayEffectClass"))
	TArray<FPP_AbilityMontageEffectWindow> MontageEffectWindows;

	/** Active effects granting any of these tags are removed when the ability activates. */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Effects")
	FGameplayTagContainer RemoveGameplayEffectsWithTagsOnActivate;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo")
	TSubclassOf<UGameplayAbility> ComboAbilityClass = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo", meta=(ClampMin="0.0", Units="Seconds"))
	float ComboWindowDuration = 2.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo")
	bool bUseComboMontageProgressBeforeInterrupt = false;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo",
		meta=(EditCondition="bUseComboMontageProgressBeforeInterrupt",
			ClampMin="0.0", ClampMax="100.0", UIMin="0.0", UIMax="100.0", Units="Percent"))
	float ComboMontageProgressBeforeInterrupt = 50.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo")
	bool bUseComboNextAbilityMontagePlayRate = false;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo",
		meta=(EditCondition="bUseComboNextAbilityMontagePlayRate", ClampMin="0.01", UIMin="0.01"))
	float ComboNextAbilityMontagePlayRate = 1.f;

	TSubclassOf<UGameplayAbility> GetComboAbilityClass() const { return ComboAbilityClass; }
	float GetComboWindowDuration() const { return ComboWindowDuration; }
	bool IsComboWindowOpen() const { return bComboWindowOpen; }
	uint32 GetActivationSequenceId() const { return ActivationSequenceId; }

	/** Returns whether this ability should temporarily pause root motion when the owning character is blocked by another character capsule. */
	bool IsRootMotionCharacterCollisionPauseEnabled() const { return bPauseRootMotionOnCharacterCollision; }

	/** Maximum forward angle used when deciding whether another character is blocking this ability's root-motion path. */
	float GetRootMotionCharacterCollisionForwardAngleDegrees() const { return RootMotionCharacterCollisionForwardAngleDegrees; }

	/** Short capsule overlap probe distance used for normal character collision detection during root motion. */
	float GetRootMotionCharacterCollisionProbeDistance() const { return RootMotionCharacterCollisionProbeDistance; }

	/** Longer fallback distance used briefly after a real block when overlap events are lost under latency. */
	float GetRootMotionCharacterCollisionFallbackProbeDistance() const
	{
		return FMath::Max(RootMotionCharacterCollisionProbeDistance, RootMotionCharacterCollisionFallbackProbeDistance);
	}

	/** Returns whether this ability asks client and server movement correction checks to be ignored while it is active. */
	bool ShouldIgnoreMovementCorrectionsDuringAbility() const { return bIgnoreMovementCorrectionsDuringAbility; }
	bool ShouldHoldRootMotionCollisionPauseUntilRelease() const { return bHoldRootMotionCollisionPauseUntilRelease; }
	bool ShouldPauseRootMotionForCharacterCollision(const ACharacter* Character) const;

protected:
	bool CanInterruptAnimatingAbility(const FGameplayAbilityActorInfo* ActorInfo) const;

	UFUNCTION(BlueprintCallable, Category="Ability|Rotation")
	void RotateAvatarToControllerYawOnActivate() const;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Rotation")
	bool bRotateToControllerYawOnActivate = false;

	// Allows this ability to break in over another active ability such as stagger or knockdown.
	UPROPERTY(EditDefaultsOnly, Category="Ability|Interrupt")
	bool bInterruptOtherAbilitiesOnActivate = false;

	/** If enabled, this ability can pause root motion when its owner is pushing into another character capsule.
	 *  Useful for slower bash / shove style attacks so the attacker does not visually drive through the target.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(ToolTip="Pause this ability's root motion when the owning character is blocked by another character capsule. Best for slower bash or shove abilities."))
	bool bPauseRootMotionOnCharacterCollision = true;

	/** Maximum forward cone angle for root-motion collision blocking.
	 *  Lower values reject side scrapes more aggressively. Higher values make the ability stop against wider angled contacts.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="Degrees",
		ToolTip="Maximum forward cone angle used when deciding whether another character should pause this ability's root motion. Lower values reject side scrapes more aggressively."))
	float RootMotionCharacterCollisionForwardAngleDegrees = 40.f;

	/** Short capsule probe distance used while root motion is active.
	 *  This is the normal overlap-based detection range.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ClampMin="0.0", UIMin="0.0", Units="cm",
		ToolTip="Normal capsule probe distance used to detect another character blocking this ability's root-motion path."))
	float RootMotionCharacterCollisionProbeDistance = 40.f;

	/** Longer fallback probe distance used after a confirmed block.
	 *  This helps smooth high-latency overlap loss without making the normal probe too wide.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ClampMin="0.0", UIMin="0.0", Units="cm",
		ToolTip="Fallback probe distance used briefly after a confirmed block, mainly to smooth lost overlap events under latency."))
	float RootMotionCharacterCollisionFallbackProbeDistance = 100.f;

	/** Temporarily asks movement correction checks to be ignored while this ability is active.
	 *  Intended for fast predicted moves such as Rush where normal correction can cause visible hitching.
	 *  Use sparingly.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(ToolTip="Temporarily ignore client movement correction checks while this ability is active. Intended for fast predicted moves such as Rush. Use sparingly."))
	bool bIgnoreMovementCorrectionsDuringAbility = false;

	/** If enabled, once collision pauses root motion, the pause is held until the montage release point.
	 *  This is mainly for very fast abilities like Rush where re-enabling root motion before release can fight prediction.
	 *  Leave disabled for pushback abilities that should resume after the target moves away.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bPauseRootMotionOnCharacterCollision",
		ToolTip="Once collision pauses root motion, keep it paused until the montage release point. Useful for very fast abilities like Rush. Leave off for pushback abilities that should resume early."))
	bool bHoldRootMotionCollisionPauseUntilRelease = false;

	UFUNCTION(BlueprintCallable, Category="Ability|Combo")
	void OpenComboWindow();

	UFUNCTION(BlueprintCallable, Category="Ability|Combo")
	void CloseComboWindow();

private:
	void ResetComboWindow();
	void InterruptOtherActiveAbilities() const;
	FActiveGameplayEffectHandle ApplyOwnedEffect(TSubclassOf<UGameplayEffect> EffectClass, float EffectLevel) const;
	void ApplyAbilityLifetimeEffects();
	void RemoveConfiguredGameplayEffectsOnActivate() const;
	float ResolveActivationMontagePlayRate(const FGameplayAbilityActorInfo* ActorInfo) const;
	void InitializeActivatedMontage(uint32 ExpectedActivationSequenceId);
	void ApplyConfiguredMontagePlayRate(uint32 ExpectedActivationSequenceId);
	void RestoreConfiguredMontagePlayRate();
	void ScheduleMontageEffectWindows(uint32 ExpectedActivationSequenceId);
	void ApplyMontageEffectWindow(uint32 ExpectedActivationSequenceId, int32 WindowIndex);
	void RemoveMontageEffectWindow(uint32 ExpectedActivationSequenceId, int32 WindowIndex);
	void CleanupOwnedEffects();

	FTimerHandle ComboWindowTimerHandle;

	bool bComboWindowOpen = false;

	uint32 ActivationSequenceId = 0;
	float ActivatedMontagePlayRate = 1.f;
	TArray<FActiveGameplayEffectHandle> AbilityLifetimeEffectHandles;
	TArray<FPP_AbilityMontageEffectWindowRuntime> MontageEffectWindowRuntime;
};

