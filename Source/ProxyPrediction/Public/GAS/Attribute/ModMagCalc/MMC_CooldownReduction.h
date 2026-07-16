// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "MMC_CooldownReduction.generated.h"


UCLASS()
class PROXYPREDICTION_API UMMC_CooldownReduction : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UMMC_CooldownReduction();

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

};
