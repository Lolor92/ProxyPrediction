#include "Combat/FunctionLibrary/PP_CombatFunctionLibrary.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"

UAbilitySystemComponent* UPP_CombatFunctionLibrary::GetAbilitySystemComponent(AActor* Actor)
{
	if (!Actor) return nullptr;

	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (APlayerState* PlayerState = Character->GetPlayerState<APlayerState>())
		{
			return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerState);
		}
	}

	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
}
