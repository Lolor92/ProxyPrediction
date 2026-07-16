#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "MMC_Strength.generated.h"

UCLASS()
class PROXYPREDICTION_API UMMC_Strength : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UMMC_Strength();

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

};
