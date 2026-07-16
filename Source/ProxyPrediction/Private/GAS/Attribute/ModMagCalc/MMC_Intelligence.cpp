#include "GAS/Attribute/ModMagCalc/MMC_Intelligence.h"
#include "Character/PP_BaseCharacter.h"


UMMC_Intelligence::UMMC_Intelligence()
{
}

float UMMC_Intelligence::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get source Actor from Spec.
	FGameplayEffectContextHandle GameplayEffectContextHandle = Spec.GetContext();
	const APP_BaseCharacter* SourceCharacter = Cast<APP_BaseCharacter>(GameplayEffectContextHandle.GetSourceObject());

	float Intelligence = 0.f;

	return 50.f;
}
