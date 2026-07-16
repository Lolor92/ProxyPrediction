#include "GAS/Attribute/ModMagCalc/MMC_MovementSpeed.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GAS/Attribute/PP_AttributeSet.h"


UMMC_MovementSpeed::UMMC_MovementSpeed()
{
	MovementSpeedDef.AttributeToCapture = UPP_AttributeSet::GetMovementSpeedAttribute();
	MovementSpeedDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	MovementSpeedDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(MovementSpeedDef);
}

float UMMC_MovementSpeed::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	float MovementSpeed = 0.f;
	GetCapturedAttributeMagnitude(MovementSpeedDef, Spec, EvaluationParameters, MovementSpeed);

	MovementSpeed = 400.f;

	return MovementSpeed;
}
