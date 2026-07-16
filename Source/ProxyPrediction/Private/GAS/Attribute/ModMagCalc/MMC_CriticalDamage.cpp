#include "GAS/Attribute/ModMagCalc/MMC_CriticalDamage.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_CriticalDamage::UMMC_CriticalDamage()
{
	AgilityDef.AttributeToCapture = UPP_AttributeSet::GetAgilityAttribute();
	AgilityDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	AgilityDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(AgilityDef);
}

float UMMC_CriticalDamage::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
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

	float Agility = 0;
	GetCapturedAttributeMagnitude(AgilityDef, Spec, EvaluationParameters, Agility);
	Agility = FMath::Max<float>(Agility, 0.f);

	float CriticalDamage = 0.2f;

	const auto CriticalDamagePercent = (Agility) / (Agility + 1200);
	return CriticalDamagePercent + CriticalDamage; // 20% base critical damage, plus any other critical damage modifiers
}
