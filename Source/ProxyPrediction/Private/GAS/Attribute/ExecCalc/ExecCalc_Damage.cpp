// Copyright ProxyPrediction

#include "GAS/Attribute/ExecCalc/ExecCalc_Damage.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "Tag/PP_NativeTags.h"

namespace
{
	struct FPPDamageStatics
	{
		DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalAttack);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicalAttack);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalDefense);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicalDefense);
		DECLARE_ATTRIBUTE_CAPTUREDEF(DamageReduction);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalDamageReduction);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicalDamageReduction);
		DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalDamageReduction);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PVPAttack);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PVPDefense);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalDefensePenetration);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicalDefensePenetration);
		DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalChance);
		DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalDamage);
		DECLARE_ATTRIBUTE_CAPTUREDEF(DamageIncrease);

		FPPDamageStatics()
		{
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, PhysicalAttack, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, MagicalAttack, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, PhysicalDefense, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, MagicalDefense, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, DamageReduction, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, PhysicalDamageReduction, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, MagicalDamageReduction, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, CriticalDamageReduction, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, PVPAttack, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, PVPDefense, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, PhysicalDefensePenetration, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, MagicalDefensePenetration, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, CriticalChance, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, CriticalDamage, Source, true);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UPP_AttributeSet, DamageIncrease, Source, true);
		}
	};

	const FPPDamageStatics& DamageStatics()
	{
		static FPPDamageStatics Statics;
		return Statics;
	}
}

UExecCalc_Damage::UExecCalc_Damage()
{
	RelevantAttributesToCapture.Add(DamageStatics().PhysicalAttackDef);
	RelevantAttributesToCapture.Add(DamageStatics().MagicalAttackDef);
	RelevantAttributesToCapture.Add(DamageStatics().PhysicalDefenseDef);
	RelevantAttributesToCapture.Add(DamageStatics().MagicalDefenseDef);
	RelevantAttributesToCapture.Add(DamageStatics().DamageReductionDef);
	RelevantAttributesToCapture.Add(DamageStatics().PhysicalDamageReductionDef);
	RelevantAttributesToCapture.Add(DamageStatics().MagicalDamageReductionDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalDamageReductionDef);
	RelevantAttributesToCapture.Add(DamageStatics().PVPAttackDef);
	RelevantAttributesToCapture.Add(DamageStatics().PVPDefenseDef);
	RelevantAttributesToCapture.Add(DamageStatics().PhysicalDefensePenetrationDef);
	RelevantAttributesToCapture.Add(DamageStatics().MagicalDefensePenetrationDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalChanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalDamageDef);
	RelevantAttributesToCapture.Add(DamageStatics().DamageIncreaseDef);
}

void UExecCalc_Damage::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluationParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float SourcePhysicalAttack = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().PhysicalAttackDef, EvaluationParameters, SourcePhysicalAttack);
	SourcePhysicalAttack = FMath::Max(SourcePhysicalAttack, 0.f);

	float SourceMagicalAttack = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().MagicalAttackDef, EvaluationParameters, SourceMagicalAttack);
	SourceMagicalAttack = FMath::Max(SourceMagicalAttack, 0.f);

	float TargetPhysicalDefense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().PhysicalDefenseDef, EvaluationParameters, TargetPhysicalDefense);
	TargetPhysicalDefense = FMath::Max(TargetPhysicalDefense, 0.f);

	float TargetMagicalDefense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().MagicalDefenseDef, EvaluationParameters, TargetMagicalDefense);
	TargetMagicalDefense = FMath::Max(TargetMagicalDefense, 0.f);

	float TargetDamageReduction = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().DamageReductionDef, EvaluationParameters, TargetDamageReduction);
	TargetDamageReduction = FMath::Clamp(TargetDamageReduction, 0.f, 1.f);

	float TargetPhysicalDamageReduction = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().PhysicalDamageReductionDef, EvaluationParameters, TargetPhysicalDamageReduction);
	TargetPhysicalDamageReduction = FMath::Clamp(TargetPhysicalDamageReduction, 0.f, 1.f);

	float TargetMagicalDamageReduction = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().MagicalDamageReductionDef, EvaluationParameters, TargetMagicalDamageReduction);
	TargetMagicalDamageReduction = FMath::Clamp(TargetMagicalDamageReduction, 0.f, 1.f);

	float TargetCriticalDamageReduction = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().CriticalDamageReductionDef, EvaluationParameters, TargetCriticalDamageReduction);
	TargetCriticalDamageReduction = FMath::Clamp(TargetCriticalDamageReduction, 0.f, 1.f);

	float SourcePVPAttack = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().PVPAttackDef, EvaluationParameters, SourcePVPAttack);
	SourcePVPAttack = FMath::Max(SourcePVPAttack, 0.f);

	float TargetPVPDefense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().PVPDefenseDef, EvaluationParameters, TargetPVPDefense);
	TargetPVPDefense = FMath::Max(TargetPVPDefense, 0.f);

	float SourcePhysicalDefensePenetration = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().PhysicalDefensePenetrationDef,
		EvaluationParameters,
		SourcePhysicalDefensePenetration);
	SourcePhysicalDefensePenetration = FMath::Clamp(SourcePhysicalDefensePenetration, 0.f, 1.f);

	float SourceMagicalDefensePenetration = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().MagicalDefensePenetrationDef,
		EvaluationParameters,
		SourceMagicalDefensePenetration);
	SourceMagicalDefensePenetration = FMath::Clamp(SourceMagicalDefensePenetration, 0.f, 1.f);

	float CriticalChance = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().CriticalChanceDef, EvaluationParameters, CriticalChance);
	CriticalChance = FMath::Clamp(CriticalChance, 0.f, 1.f);

	float CriticalDamage = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().CriticalDamageDef, EvaluationParameters, CriticalDamage);
	CriticalDamage = FMath::Max(CriticalDamage, 0.f);

	float SourceDamageIncrease = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().DamageIncreaseDef, EvaluationParameters, SourceDamageIncrease);
	SourceDamageIncrease = FMath::Max(SourceDamageIncrease, 0.f);

	if (Spec.DynamicGrantedTags.HasTag(TAG_Trigger_Hit_Dodged))
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UPP_AttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, 0.f));
		return;
	}

	const float PhysicalAttackPercent = FMath::Max(
		Spec.GetSetByCallerMagnitude(TAG_Damage_Type_Physical, false, 0.f), 0.0f);
	const float MagicalAttackPercent = FMath::Max(
		Spec.GetSetByCallerMagnitude(TAG_Damage_Type_Magical, false, 0.f), 0.0f);

	const float PhysicalDamage = CalculateBaseDamage(
		SourcePhysicalAttack,
		TargetPhysicalDefense,
		SourcePhysicalDefensePenetration,
		PhysicalAttackPercent) * (1.0f - TargetPhysicalDamageReduction);
	const float MagicalDamage = CalculateBaseDamage(
		SourceMagicalAttack,
		TargetMagicalDefense,
		SourceMagicalDefensePenetration,
		MagicalAttackPercent) * (1.0f - TargetMagicalDamageReduction);

	float Damage = PhysicalDamage + MagicalDamage;
	Damage *= 1.0f + SourceDamageIncrease;
	Damage = ApplySharedCalculations(
		Damage, TargetDamageReduction, SourcePVPAttack, TargetPVPDefense, false);

	if (FMath::FRand() <= CriticalChance)
	{
		Damage *= 1.f + CriticalDamage;
		Damage *= 1.f - TargetCriticalDamageReduction;

		FGameplayEffectSpec& MutableSpec = const_cast<FGameplayEffectSpec&>(Spec);
		MutableSpec.DynamicGrantedTags.AddTag(TAG_Hit_Critical);
	}

	Damage *= FMath::FRandRange(0.9f, 1.1f);
	Damage = FMath::Max(0.f, Damage);

	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		UPP_AttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage));
}

float UExecCalc_Damage::CalculateBaseDamage(
	const float SourceAttack,
	const float TargetDefense,
	const float SourceDefensePenetration,
	const float DamagePercent) const
{
	const float EffectiveDefense = TargetDefense * (1.f - SourceDefensePenetration);
	float DamageReduction = EffectiveDefense / (EffectiveDefense + 1000.f);
	const float DamageValue = SourceAttack * DamagePercent;
	const float AttackDefenseRatio = DamageValue / (EffectiveDefense + 1000.f);
	DamageReduction *= 1.f - AttackDefenseRatio;

	return DamageValue * (1.f - DamageReduction);
}

float UExecCalc_Damage::ApplySharedCalculations(
	float Damage,
	const float TargetDamageReduction,
	const float SourcePVPAttack,
	const float TargetPVPDefense,
	const bool bIsPVP)
{
	Damage *= 1.f - TargetDamageReduction;

	if (bIsPVP)
	{
		Damage *= 1.f + SourcePVPAttack - TargetPVPDefense;
	}

	return Damage;
}
