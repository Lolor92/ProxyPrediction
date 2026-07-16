#include "GAS/Attribute/ModMagCalc/MMC_DamageReduction.h"

UMMC_DamageReduction::UMMC_DamageReduction()
{
}

float UMMC_DamageReduction::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	return 1.f;
}
