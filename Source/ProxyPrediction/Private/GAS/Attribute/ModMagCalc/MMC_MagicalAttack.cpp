#include "GAS/Attribute/ModMagCalc/MMC_MagicalAttack.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_MagicalAttack::UMMC_MagicalAttack()
{
	IntelligenceDef.AttributeToCapture = UPP_AttributeSet::GetIntelligenceAttribute();
	IntelligenceDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	IntelligenceDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(IntelligenceDef);

	AgilityDef.AttributeToCapture = UPP_AttributeSet::GetAgilityAttribute();
	AgilityDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	AgilityDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(AgilityDef);
}

float UMMC_MagicalAttack::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get source Actor from Spec
	FGameplayEffectContextHandle GameplayEffectContextHandle = Spec.GetContext();
	const APP_BaseCharacter* SourceCharacter = Cast<APP_BaseCharacter>(GameplayEffectContextHandle.GetSourceObject());

	float Intelligence = 0.f;
	GetCapturedAttributeMagnitude(IntelligenceDef, Spec, EvaluationParameters, Intelligence);
	Intelligence = FMath::Max(Intelligence, 0.f);

	float Agility = 0.f;
	GetCapturedAttributeMagnitude(AgilityDef, Spec, EvaluationParameters, Agility);
	Agility = FMath::Max(Agility, 0.f);

	return (Intelligence * 0.5f) + (Agility * 0.1f);
}
