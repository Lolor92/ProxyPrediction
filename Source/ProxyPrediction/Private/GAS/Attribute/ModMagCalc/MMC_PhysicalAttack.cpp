#include "GAS/Attribute/ModMagCalc/MMC_PhysicalAttack.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_PhysicalAttack::UMMC_PhysicalAttack()
{
	StrengthDef.AttributeToCapture = UPP_AttributeSet::GetStrengthAttribute();
	StrengthDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	StrengthDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(StrengthDef);

	AgilityDef.AttributeToCapture = UPP_AttributeSet::GetAgilityAttribute();
	AgilityDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	AgilityDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(AgilityDef);
}

float UMMC_PhysicalAttack::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target.
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get source Actor from Spec.
	/*FGameplayEffectContextHandle GameplayEffectContextHandle = Spec.GetContext();
	const APP_BaseCharacter* SourceCharacter = Cast<APP_BaseCharacter>(GameplayEffectContextHandle.GetSourceObject());*/

	float Strength = 0.f;
	GetCapturedAttributeMagnitude(StrengthDef, Spec, EvaluationParameters, Strength);
	Strength = FMath::Max(Strength, 0.f);

	float Agility = 0.f;
	GetCapturedAttributeMagnitude(AgilityDef, Spec, EvaluationParameters, Agility);
	Agility = FMath::Max(Agility, 0.f);


	return (Strength * 0.5f) + (Agility * 0.1f);
}
