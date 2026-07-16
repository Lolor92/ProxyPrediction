// Copyright ProxyPrediction

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "ExecCalc_Damage.generated.h"

UCLASS()
class PROXYPREDICTION_API UExecCalc_Damage : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UExecCalc_Damage();

	float CalculateBaseDamage(
		float SourceAttack,
		float TargetDefense,
		float SourceDefensePenetration,
		float DamagePercent) const;

	static float ApplySharedCalculations(
		float Damage,
		float TargetDamageReduction,
		float SourcePVPAttack,
		float TargetPVPDefense,
		bool bIsPVP);

	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
