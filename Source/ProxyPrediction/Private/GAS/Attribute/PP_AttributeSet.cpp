// Copyright ProxyPrediction

#include "GAS/Attribute/PP_AttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "GameplayEffectExtension.h"
#include "Tag/PP_NativeTags.h"
#include "UI/PP_CombatTextComponent.h"
#include "UI/PP_CombatTextTypes.h"

namespace
{
	FVector PP_GetCombatTextLocation(const FPP_EffectProperties& Props)
	{
		if (const FHitResult* HitResult = Props.EffectContextHandle.GetHitResult();
			HitResult && HitResult->bBlockingHit && !HitResult->ImpactPoint.ContainsNaN())
		{
			return HitResult->ImpactPoint;
		}

		if (Props.TargetAvatarActor)
		{
			FVector Origin;
			FVector Extent;
			Props.TargetAvatarActor->GetActorBounds(true, Origin, Extent);
			return Origin + FVector(0.0f, 0.0f, Extent.Z);
		}

		return FVector::ZeroVector;
	}

	void PP_ShowCombatText(
		AActor* RecipientAvatar,
		const float Amount,
		const FVector& WorldLocation,
		const EPP_CombatTextType Type,
		const bool bCritical = false)
	{
		if (UPP_CombatTextComponent* CombatText =
			RecipientAvatar ? RecipientAvatar->FindComponentByClass<UPP_CombatTextComponent>() : nullptr)
		{
			CombatText->ShowCombatTextToOwner(Amount, WorldLocation, Type, bCritical);
		}
	}
}

void UPP_AttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, Energy, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MaxEnergy, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, Strength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, Intelligence, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, Agility, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, PhysicalAttack, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MagicalAttack, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, PhysicalDefense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MagicalDefense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, CriticalChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, CriticalDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, AttackSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MovementSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MountSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, CooldownReduction, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, HealthRegen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, EnergyRegen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, StaminaRegen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, HealingEffectiveness, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, ShieldingEffectiveness, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, DamageReduction, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, PhysicalDamageReduction, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MagicalDamageReduction, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, CriticalDamageReduction, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, PVPAttack, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, PVPDefense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, PhysicalDefensePenetration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, MagicalDefensePenetration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UPP_AttributeSet, DamageIncrease, COND_None, REPNOTIFY_Always);
}

void UPP_AttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	if (Attribute == GetEnergyAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxEnergy());
	}
	if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	if (FGameplayAbilityActorInfo* ActorInfo = GetActorInfo())
	{
		if (Attribute == GetMovementSpeedAttribute())
		{
			if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(ActorInfo->MovementComponent))
			{
				CharacterMovement->MaxWalkSpeed = NewValue;
			}
		}
	}
}

void UPP_AttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
}

void UPP_AttributeSet::SetEffectProperties(const FGameplayEffectModCallbackData& Data, FPP_EffectProperties& Props) const
{
	// Source = causer of the effect, Target = target of the effect (owner of this AS)

	Props.EffectContextHandle = Data.EffectSpec.GetContext();
	Props.SourceASC = Props.EffectContextHandle.GetOriginalInstigatorAbilitySystemComponent();

	if (IsValid(Props.SourceASC) && Props.SourceASC->AbilityActorInfo.IsValid() && Props.SourceASC->AbilityActorInfo->AvatarActor.IsValid())
	{
		Props.SourceAvatarActor = Props.SourceASC->AbilityActorInfo->AvatarActor.Get();
		Props.SourceController = Props.SourceASC->AbilityActorInfo->PlayerController.Get();
		if (Props.SourceController == nullptr && Props.SourceAvatarActor != nullptr)
		{
			if (const APawn* Pawn = Cast<APawn>(Props.SourceAvatarActor))
			{
				Props.SourceController = Pawn->GetController();
			}
		}
		if (Props.SourceController)
		{
			Props.SourceCharacter = Cast<ACharacter>(Props.SourceController->GetPawn());
		}
	}

	if (Data.Target.AbilityActorInfo.IsValid() && Data.Target.AbilityActorInfo->AvatarActor.IsValid())
	{
		Props.TargetAvatarActor = Data.Target.AbilityActorInfo->AvatarActor.Get();
		Props.TargetController = Data.Target.AbilityActorInfo->PlayerController.Get();
		Props.TargetCharacter = Cast<ACharacter>(Props.TargetAvatarActor);
		Props.TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Props.TargetAvatarActor);
	}
}

void UPP_AttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	FPP_EffectProperties Props;
	SetEffectProperties(Data, Props);

	const FGameplayAttribute Attribute = Data.EvaluatedData.Attribute;

	if (Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	if (Attribute == GetEnergyAttribute())
	{
		SetEnergy(FMath::Clamp(GetEnergy(), 0.f, GetMaxEnergy()));
	}
	if (Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));
	}
	if (Attribute == GetIncomingDamageAttribute())
	{
		const float Damage = static_cast<float>(FMath::RoundToInt(GetIncomingDamage()));
		SetIncomingDamage(0.f);
		if (Damage <= 0.0f) return;

		const float CurrentHealth = GetHealth();
		const float NewHealth = FMath::Clamp(CurrentHealth - Damage, 0.f, GetMaxHealth());
		const float AppliedDamage = CurrentHealth - NewHealth;

		if (!FMath::IsNearlyEqual(NewHealth, CurrentHealth))
		{
			SetHealth(NewHealth);
		}

		if (AppliedDamage > 0.0f)
		{
			FGameplayTagContainer GrantedTags;
			Data.EffectSpec.GetAllGrantedTags(GrantedTags);
			const bool bCritical = GrantedTags.HasTagExact(TAG_Hit_Critical);
			const FVector WorldLocation = PP_GetCombatTextLocation(Props);

			PP_ShowCombatText(
				Props.TargetAvatarActor, AppliedDamage, WorldLocation,
				EPP_CombatTextType::DamageReceived, bCritical);
			if (Props.SourceAvatarActor && Props.SourceAvatarActor != Props.TargetAvatarActor)
			{
				PP_ShowCombatText(
					Props.SourceAvatarActor, AppliedDamage, WorldLocation,
					EPP_CombatTextType::DamageDealt, bCritical);
			}
		}
	}
	if (Attribute == GetIncomingHealingAttribute())
	{
		const float Healing = static_cast<float>(FMath::RoundToInt(GetIncomingHealing()));
		SetIncomingHealing(0.0f);
		if (Healing <= 0.0f) return;

		const float CurrentHealth = GetHealth();
		const float NewHealth = FMath::Clamp(CurrentHealth + Healing, 0.0f, GetMaxHealth());
		const float AppliedHealing = NewHealth - CurrentHealth;

		if (!FMath::IsNearlyEqual(NewHealth, CurrentHealth))
		{
			SetHealth(NewHealth);
		}

		if (AppliedHealing > 0.0f)
		{
			const FVector WorldLocation = PP_GetCombatTextLocation(Props);
			PP_ShowCombatText(
				Props.TargetAvatarActor, AppliedHealing, WorldLocation,
				EPP_CombatTextType::HealingReceived);
			if (Props.SourceAvatarActor && Props.SourceAvatarActor != Props.TargetAvatarActor)
			{
				PP_ShowCombatText(
					Props.SourceAvatarActor, AppliedHealing, WorldLocation,
					EPP_CombatTextType::HealingDealt);
			}
		}
	}
}

void UPP_AttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, Health, OldHealth);
}

void UPP_AttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MaxHealth, OldMaxHealth);
}

void UPP_AttributeSet::OnRep_Energy(const FGameplayAttributeData& OldEnergy) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, Energy, OldEnergy);
}

void UPP_AttributeSet::OnRep_MaxEnergy(const FGameplayAttributeData& OldMaxEnergy) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MaxEnergy, OldMaxEnergy);
}

void UPP_AttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, Stamina, OldStamina);
}

void UPP_AttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MaxStamina, OldMaxStamina);
}

void UPP_AttributeSet::OnRep_Strength(const FGameplayAttributeData& OldStrength) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, Strength, OldStrength);
}

void UPP_AttributeSet::OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, Intelligence, OldIntelligence);
}

void UPP_AttributeSet::OnRep_Agility(const FGameplayAttributeData& OldAgility) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, Agility, OldAgility);
}

void UPP_AttributeSet::OnRep_PhysicalAttack(const FGameplayAttributeData& OldPhysicalAttack) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, PhysicalAttack, OldPhysicalAttack);
}

void UPP_AttributeSet::OnRep_MagicalAttack(const FGameplayAttributeData& OldMagicalAttack) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MagicalAttack, OldMagicalAttack);
}

void UPP_AttributeSet::OnRep_PhysicalDefense(const FGameplayAttributeData& OldPhysicalDefense) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, PhysicalDefense, OldPhysicalDefense);
}

void UPP_AttributeSet::OnRep_MagicalDefense(const FGameplayAttributeData& OldMagicalDefense) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MagicalDefense, OldMagicalDefense);
}

void UPP_AttributeSet::OnRep_CriticalChance(const FGameplayAttributeData& OldCriticalChance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, CriticalChance, OldCriticalChance);
}

void UPP_AttributeSet::OnRep_CriticalDamage(const FGameplayAttributeData& OldCriticalDamage) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, CriticalDamage, OldCriticalDamage);
}

void UPP_AttributeSet::OnRep_AttackSpeed(const FGameplayAttributeData& OldAttackSpeed) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, AttackSpeed, OldAttackSpeed);
}

void UPP_AttributeSet::OnRep_MovementSpeed(const FGameplayAttributeData& OldMovementSpeed) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MovementSpeed, OldMovementSpeed);
}

void UPP_AttributeSet::OnRep_MountSpeed(const FGameplayAttributeData& OldMountSpeed) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MountSpeed, OldMountSpeed);
}

void UPP_AttributeSet::OnRep_CooldownReduction(const FGameplayAttributeData& OldCooldownReduction) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, CooldownReduction, OldCooldownReduction);
}

void UPP_AttributeSet::OnRep_HealthRegen(const FGameplayAttributeData& OldHealthRegen) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, HealthRegen, OldHealthRegen);
}

void UPP_AttributeSet::OnRep_EnergyRegen(const FGameplayAttributeData& OldEnergyRegen) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, EnergyRegen, OldEnergyRegen);
}

void UPP_AttributeSet::OnRep_StaminaRegen(const FGameplayAttributeData& OldStaminaRegen) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, StaminaRegen, OldStaminaRegen);
}

void UPP_AttributeSet::OnRep_HealingEffectiveness(const FGameplayAttributeData& OldHealingEffectiveness) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, HealingEffectiveness, OldHealingEffectiveness);
}

void UPP_AttributeSet::OnRep_ShieldingEffectiveness(const FGameplayAttributeData& OldShieldingEffectiveness) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, ShieldingEffectiveness, OldShieldingEffectiveness);
}

void UPP_AttributeSet::OnRep_DamageReduction(const FGameplayAttributeData& OldDamageReduction) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, DamageReduction, OldDamageReduction);
}

void UPP_AttributeSet::OnRep_PhysicalDamageReduction(const FGameplayAttributeData& OldPhysicalDamageReduction) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, PhysicalDamageReduction, OldPhysicalDamageReduction);
}

void UPP_AttributeSet::OnRep_MagicalDamageReduction(const FGameplayAttributeData& OldMagicalDamageReduction) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MagicalDamageReduction, OldMagicalDamageReduction);
}

void UPP_AttributeSet::OnRep_CriticalDamageReduction(const FGameplayAttributeData& OldCriticalDamageReduction) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, CriticalDamageReduction, OldCriticalDamageReduction);
}

void UPP_AttributeSet::OnRep_PVPAttack(const FGameplayAttributeData& OldPVPAttack) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, PVPAttack, OldPVPAttack);
}

void UPP_AttributeSet::OnRep_PVPDefense(const FGameplayAttributeData& OldPVPDefense) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, PVPDefense, OldPVPDefense);
}

void UPP_AttributeSet::OnRep_PhysicalDefensePenetration(const FGameplayAttributeData& OldPhysicalDefensePenetration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, PhysicalDefensePenetration, OldPhysicalDefensePenetration);
}

void UPP_AttributeSet::OnRep_MagicalDefensePenetration(const FGameplayAttributeData& OldMagicalDefensePenetration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, MagicalDefensePenetration, OldMagicalDefensePenetration);
}

void UPP_AttributeSet::OnRep_DamageIncrease(const FGameplayAttributeData& OldDamageIncrease) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UPP_AttributeSet, DamageIncrease, OldDamageIncrease);
}
