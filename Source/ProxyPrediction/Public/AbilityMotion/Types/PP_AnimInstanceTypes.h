#pragma once

#include "CoreMinimal.h"
#include "PP_AnimInstanceTypes.generated.h"

// Anim-facing copy of the replicated ability movement state.
USTRUCT(BlueprintType)
struct FPP_AbilityMotionState
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

