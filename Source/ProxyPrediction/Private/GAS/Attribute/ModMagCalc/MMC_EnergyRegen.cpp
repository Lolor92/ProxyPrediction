#include "GAS/Attribute/ModMagCalc/MMC_EnergyRegen.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_EnergyRegen::UMMC_EnergyRegen()
{
	MaxEnergyDef.AttributeToCapture = UPP_AttributeSet::GetMaxEnergyAttribute();
	MaxEnergyDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	MaxEnergyDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(MaxEnergyDef);

	CurrentEnergyDef.AttributeToCapture = UPP_AttributeSet::GetEnergyAttribute();
	CurrentEnergyDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	CurrentEnergyDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(CurrentEnergyDef);
}

float UMMC_EnergyRegen::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get target Actor from Spec
	FGameplayEffectContextHandle GameplayEffectContextHandle = Spec.GetContext();
	const APP_BaseCharacter* Character = Cast<APP_BaseCharacter>(GameplayEffectContextHandle.GetSourceObject());

	float MaxEnergy = 0.f;
	GetCapturedAttributeMagnitude(MaxEnergyDef, Spec, EvaluationParameters, MaxEnergy);
	MaxEnergy = FMath::Max(MaxEnergy, 0.f);

	float CurrentEnergy = 0.f;
	GetCapturedAttributeMagnitude(CurrentEnergyDef, Spec, EvaluationParameters, CurrentEnergy);
	CurrentEnergy = FMath::Max(CurrentEnergy, 0.f);

	if (CurrentEnergy == MaxEnergy || MaxEnergy <= 0.f)
	{
		return 0.f;
	}

	const float Regen = 25.f;
	return FMath::RoundToFloat(Regen);
}
