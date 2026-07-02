#pragma once

#include "CoreMinimal.h"
#include "SyncAbilityMotionTypes.generated.h"

// Replicated state shared between abilities, movement, and animation.
USTRUCT(BlueprintType)
struct FSyncAbilityMotionState
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 bCanBlendMontage : 1;

	UPROPERTY()
	uint8 bShouldBlendLowerBody : 1;

	UPROPERTY()
	uint8 bRootMotionEnabled : 1;

	UPROPERTY()
	uint8 bMovementInputSuppressed : 1;

	FSyncAbilityMotionState()
		: bCanBlendMontage(false)
		, bShouldBlendLowerBody(false)
		, bRootMotionEnabled(true)
		, bMovementInputSuppressed(false)
	{
	}

	bool operator==(const FSyncAbilityMotionState& Other) const
	{
		return bCanBlendMontage == Other.bCanBlendMontage
			&& bShouldBlendLowerBody == Other.bShouldBlendLowerBody
			&& bRootMotionEnabled == Other.bRootMotionEnabled
			&& bMovementInputSuppressed == Other.bMovementInputSuppressed;
	}

	bool operator!=(const FSyncAbilityMotionState& Other) const
	{
		return !(*this == Other);
	}
};

USTRUCT(BlueprintType)
struct FSyncAbilityMotionMontageLockout
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
};
