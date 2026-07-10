#include "Input/FunctionLibrary/PP_InputFunctionLibrary.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"

UAbilitySystemComponent* UPP_InputFunctionLibrary::GetAbilitySystemComponent(AActor* Actor)
{
	if (!Actor) return nullptr;

	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (APlayerState* PlayerState = Character->GetPlayerState<APlayerState>())
		{
			if (UAbilitySystemComponent* PlayerStateASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerState))
			{
				return PlayerStateASC;
			}
		}
	}

	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
}

