#include "GAS/Attribute/ModMagCalc/MMC_DamageIncrease.h"

UMMC_DamageIncrease::UMMC_DamageIncrease()
{
}

float UMMC_DamageIncrease::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	return 1;
}
