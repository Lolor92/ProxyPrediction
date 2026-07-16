#pragma once

#include "CoreMinimal.h"
#include "PP_CombatTextTypes.generated.h"

UENUM(BlueprintType)
enum class EPP_CombatTextType : uint8
{
	DamageDealt UMETA(DisplayName="Damage Dealt"),
	DamageReceived UMETA(DisplayName="Damage Received"),
	HealingDealt UMETA(DisplayName="Healing Dealt"),
	HealingReceived UMETA(DisplayName="Healing Received"),
	AttackBlocked UMETA(DisplayName="Attack Blocked"),
	AttackDodged UMETA(DisplayName="Attack Dodged"),
	AttackSuperArmored UMETA(DisplayName="Attack Super Armored")
};
