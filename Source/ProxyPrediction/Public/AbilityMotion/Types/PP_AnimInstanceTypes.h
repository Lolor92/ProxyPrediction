#pragma once

#include "CoreMinimal.h"
#include "PP_AnimInstanceTypes.generated.h"

/** Compact ability movement state shared by the owner, server, and animation proxies. */
USTRUCT(BlueprintType)
struct FPP_AbilityMotionState
{
	GENERATED_BODY()

	/** Active montage has reached the point where another montage may blend in. */
	UPROPERTY()
	uint8 bCanBlendMontage : 1;

	/** Locomotion may drive the lower body while the upper-body montage continues. */
	UPROPERTY()
	uint8 bShouldBlendLowerBody : 1;

	/** Animation root motion may drive the character. */
	UPROPERTY()
	uint8 bRootMotionEnabled : 1;

	/** Player movement input is ignored while the ability owns movement. */
	UPROPERTY()
	uint8 bMovementInputSuppressed : 1;

	FPP_AbilityMotionState()
		: bCanBlendMontage(false)
		, bShouldBlendLowerBody(false)
		, bRootMotionEnabled(true)
		, bMovementInputSuppressed(false)
	{
	}

	bool operator==(const FPP_AbilityMotionState& Other) const
	{
		return bCanBlendMontage == Other.bCanBlendMontage
			&& bShouldBlendLowerBody == Other.bShouldBlendLowerBody
			&& bRootMotionEnabled == Other.bRootMotionEnabled
			&& bMovementInputSuppressed == Other.bMovementInputSuppressed;
	}

	bool operator!=(const FPP_AbilityMotionState& Other) const
	{
		return !(*this == Other);
	}
};

