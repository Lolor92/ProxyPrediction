#include "GAS/Attribute/ModMagCalc/MMC_MaxEnergy.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_MaxEnergy::UMMC_MaxEnergy()
{
	IntelligenceDef.AttributeToCapture = UPP_AttributeSet::GetIntelligenceAttribute();
	IntelligenceDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	IntelligenceDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(IntelligenceDef);
}

float UMMC_MaxEnergy::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
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

	float Intelligence = 0;
	GetCapturedAttributeMagnitude(IntelligenceDef, Spec, EvaluationParameters, Intelligence);
	Intelligence = FMath::Max<float>(Intelligence, 0.f);

	float MaxEnergy = 100.f;

	return (Intelligence * 1.05) + MaxEnergy;
}
