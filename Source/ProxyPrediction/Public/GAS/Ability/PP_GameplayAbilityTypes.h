#pragma once

#include "CoreMinimal.h"
#include "PP_GameplayAbilityTypes.generated.h"

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
};

