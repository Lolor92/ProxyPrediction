#include "GAS/Attribute/ModMagCalc/MMC_CooldownReduction.h"

UMMC_CooldownReduction::UMMC_CooldownReduction()
{
}

float UMMC_CooldownReduction::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const float CoolDownReduction = 0.f;

	return CoolDownReduction;
}
