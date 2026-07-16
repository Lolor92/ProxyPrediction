#include "GAS/Attribute/ModMagCalc/MMC_HealingEffectiveness.h"
#include "Character/PP_BaseCharacter.h"

UMMC_HealingEffectiveness::UMMC_HealingEffectiveness()
{
}

float UMMC_HealingEffectiveness::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get target Actor from Spec
	const FGameplayEffectContextHandle GameplayEffectContextHandle = Spec.GetContext();
	const APP_BaseCharacter* Character = Cast<APP_BaseCharacter>(GameplayEffectContextHandle.GetSourceObject());

	return 1.f;
}
