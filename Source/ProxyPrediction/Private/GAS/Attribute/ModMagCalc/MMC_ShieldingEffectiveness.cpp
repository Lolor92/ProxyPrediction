#include "GAS/Attribute/ModMagCalc/MMC_ShieldingEffectiveness.h"
#include "Character/PP_BaseCharacter.h"

UMMC_ShieldingEffectiveness::UMMC_ShieldingEffectiveness()
{
}

float UMMC_ShieldingEffectiveness::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get target Actor from Spec
	/*const FGameplayEffectContextHandle GameplayEffectContextHandle = Spec.GetContext();
	const APP_BaseCharacter* Character = Cast<APP_BaseCharacter>(GameplayEffectContextHandle.GetSourceObject());
	*/

	return 1.f;
}
