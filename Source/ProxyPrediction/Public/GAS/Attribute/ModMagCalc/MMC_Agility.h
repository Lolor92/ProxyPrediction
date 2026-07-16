// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "MMC_Agility.generated.h"

UCLASS()
class PROXYPREDICTION_API UMMC_Agility : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UMMC_Agility();

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

};
