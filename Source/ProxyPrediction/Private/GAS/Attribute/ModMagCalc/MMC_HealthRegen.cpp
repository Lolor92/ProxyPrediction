#include "GAS/Attribute/ModMagCalc/MMC_HealthRegen.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_HealthRegen::UMMC_HealthRegen()
{
	MaxHealthDef.AttributeToCapture = UPP_AttributeSet::GetMaxHealthAttribute();
	MaxHealthDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	MaxHealthDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(MaxHealthDef);

	CurrentHealthDef.AttributeToCapture = UPP_AttributeSet::GetHealthAttribute();
	CurrentHealthDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	CurrentHealthDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(CurrentHealthDef);
}

float UMMC_HealthRegen::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
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

	float MaxHealth = 0.f;
	GetCapturedAttributeMagnitude(MaxHealthDef, Spec, EvaluationParameters, MaxHealth);
	MaxHealth = FMath::Max(MaxHealth, 0.f);

	float CurrentHealth = 0.f;
	GetCapturedAttributeMagnitude(CurrentHealthDef, Spec, EvaluationParameters, CurrentHealth);
	CurrentHealth = FMath::Max(CurrentHealth, 0.f);

	if (CurrentHealth <= 0.f || MaxHealth <= 0.f)
	{
		return 0.f;
	}

	const float Regen = MaxHealth * 0.0005f;
	return FMath::RoundToFloat(Regen);
}
