// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "MMC_MagicalAttack.generated.h"


UCLASS()
class PROXYPREDICTION_API UMMC_MagicalAttack : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UMMC_MagicalAttack();

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

private:
	FGameplayEffectAttributeCaptureDefinition IntelligenceDef;
	FGameplayEffectAttributeCaptureDefinition AgilityDef;


};
