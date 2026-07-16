#include "GAS/Attribute/ModMagCalc/MMC_Cooldown.h"


UMMC_Cooldown::UMMC_Cooldown()
{
}

float UMMC_Cooldown::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	return Super::CalculateBaseMagnitude_Implementation(Spec);
}
