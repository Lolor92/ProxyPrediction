// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "MMC_PhysicalDefense.generated.h"


UCLASS()
class PROXYPREDICTION_API UMMC_PhysicalDefense : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UMMC_PhysicalDefense();

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

private:
	FGameplayEffectAttributeCaptureDefinition StrengthDef;
};
