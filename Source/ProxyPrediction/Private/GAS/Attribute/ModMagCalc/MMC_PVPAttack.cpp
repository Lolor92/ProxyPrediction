#include "GAS/Attribute/ModMagCalc/MMC_PVPAttack.h"
#include <AbilitySystemComponent.h>
#include "Character/PP_BaseCharacter.h"

UMMC_PVPAttack::UMMC_PVPAttack()
{
}

float UMMC_PVPAttack::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather tags from source and target.
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// Get the source actor from the ability system.
	/*UAbilitySystemComponent* SourceASC = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
	AActor* SourceAvatar = SourceASC ? SourceASC->GetAvatarActor() : nullptr;
	APP_BaseCharacter* SourceCharacter = Cast<APP_BaseCharacter>(SourceAvatar);*/

	float PVPAttack = 0.f;

	return PVPAttack;
}
