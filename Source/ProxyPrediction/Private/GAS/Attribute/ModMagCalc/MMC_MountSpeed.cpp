#include "GAS/Attribute/ModMagCalc/MMC_MountSpeed.h"

UMMC_MountSpeed::UMMC_MountSpeed()
{

}

float UMMC_MountSpeed::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	const float MountSpeed = 700.f;
	return MountSpeed;
}
