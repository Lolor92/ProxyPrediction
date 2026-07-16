#include "GAS/Attribute/ModMagCalc/MMC_StaminaRegen.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Character/PP_BaseCharacter.h"

UMMC_StaminaRegen::UMMC_StaminaRegen()
{
	MaxStaminaDef.AttributeToCapture = UPP_AttributeSet::GetMaxStaminaAttribute();
	MaxStaminaDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	MaxStaminaDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(MaxStaminaDef);

	CurrentStaminaDef.AttributeToCapture = UPP_AttributeSet::GetStaminaAttribute();
	CurrentStaminaDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	CurrentStaminaDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(CurrentStaminaDef);
}

float UMMC_StaminaRegen::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
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

	float MaxStamina = 0.f;
	GetCapturedAttributeMagnitude(MaxStaminaDef, Spec, EvaluationParameters, MaxStamina);
	MaxStamina = FMath::Max(MaxStamina, 0.f);

	float CurrentStamina = 0.f;
	GetCapturedAttributeMagnitude(CurrentStaminaDef, Spec, EvaluationParameters, CurrentStamina);
	CurrentStamina = FMath::Max(CurrentStamina, 0.f);

	if (CurrentStamina == MaxStamina || MaxStamina <= 0.f)
	{
		return 0.f;
	}

	/*if (Character)
	{
		if (const USyncCombatComponent* CombatComponent = Character->GetCombatComponent())
		{
			if (CombatComponent->IsBlockingActive())
			{
				return 0.f;
			}
		}
	}*/

	const float Regen = 25.f;
	return FMath::RoundToFloat(Regen);
}
