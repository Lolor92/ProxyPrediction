#include "GAS/Attribute/ModMagCalc/MMC_PhysicalDamageReduction.h"

UMMC_PhysicalDamageReduction::UMMC_PhysicalDamageReduction()
{
}

float UMMC_PhysicalDamageReduction::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	return 0.f;
}
