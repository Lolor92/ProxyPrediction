#include "GAS/Attribute/ModMagCalc/MMC_MaxStamina.h"

UMMC_MaxStamina::UMMC_MaxStamina()
{
}

float UMMC_MaxStamina::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	return 100.f;
}
