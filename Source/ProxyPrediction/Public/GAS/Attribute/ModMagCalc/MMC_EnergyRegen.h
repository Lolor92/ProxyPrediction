// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "MMC_EnergyRegen.generated.h"


UCLASS()
class PROXYPREDICTION_API UMMC_EnergyRegen : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UMMC_EnergyRegen();

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

	FGameplayEffectAttributeCaptureDefinition MaxEnergyDef;
	FGameplayEffectAttributeCaptureDefinition CurrentEnergyDef;

};
