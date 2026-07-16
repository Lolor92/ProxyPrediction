#include "GAS/Attribute/ModMagCalc/MMC_Cost.h"
#include <AbilitySystemBlueprintLibrary.h>

UMMC_Cost::UMMC_Cost()
{
}

float UMMC_Cost::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const UGameplayAbility* SourceAbility = Spec.GetEffectContext().GetAbilityInstance_NotReplicated();

	return 1;
}
