#pragma once

#include "CoreMinimal.h"
#include "PP_GameplayAbilityTypes.generated.h"

class UGameplayEffect;

/** Gameplay Effect applied by an ability with a configurable spec level. */
USTRUCT(BlueprintType)
struct FPP_AbilityOwnedEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Ability Effect")
	TSubclassOf<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, Category="Ability Effect", meta=(ClampMin="0.0"))
	float EffectLevel = 1.0f;
};

/** One-shot Gameplay Effect window driven by normalized montage position. */
USTRUCT(BlueprintType)
struct FPP_AbilityMontageEffectWindow
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Montage Effect Window")
	TSubclassOf<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, Category="Montage Effect Window", meta=(ClampMin="0.0"))
	float EffectLevel = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category="Montage Effect Window",
		meta=(ClampMin="0.0", ClampMax="100.0", UIMin="0.0", UIMax="100.0", Units="Percent"))
	float ApplyAtMontagePercent = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category="Montage Effect Window",
		meta=(ClampMin="0.0", ClampMax="100.0", UIMin="0.0", UIMax="100.0", Units="Percent"))
	float RemoveAtMontagePercent = 100.0f;
};

USTRUCT(BlueprintType)
struct FPP_AbilityMotionMontageLockout
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout", meta=(InLineEditConditionToggle))
	bool bUseMontageProgressLockout = false;

	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout", meta=(EditCondition="bUseMontageProgressLockout",
		ClampMin="0.0", ClampMax="100.0", UIMin="0.0", UIMax="100.0", Units="Percent"))
	float MontageProgressBeforeInterrupt = 0.f;

	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout", meta=(EditCondition="bUseMontageProgressLockout"))
	bool bBlockAbilityActivationUntilUnlock = true;

	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout")
	bool bBypassMontageLockout = false;

	/** How early the authoritative server may accept a remote client's interrupt request.
	 *  The predicting client unlocks at the exact configured percentage; this small
	 *  server-only tolerance absorbs client/server montage tick-boundary differences
	 *  without adding input latency to the local player.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout", meta=(ClampMin="0.0", UIMin="0.0", Units="Seconds"))
	float ServerInterruptEarlyAcceptanceTolerance = 0.05f;
};

