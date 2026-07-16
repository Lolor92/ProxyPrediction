#include "GAS/Attribute/ModMagCalc/MMC_MagicalDamageReduction.h"

UMMC_MagicalDamageReduction::UMMC_MagicalDamageReduction()
{
}

float UMMC_MagicalDamageReduction::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	return 0.f;
}
