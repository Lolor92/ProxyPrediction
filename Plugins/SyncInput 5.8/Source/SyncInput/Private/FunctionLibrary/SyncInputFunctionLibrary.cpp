#include "FunctionLibrary/SyncInputFunctionLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"

UAbilitySystemComponent* USyncInputFunctionLibrary::GetAbilitySystemComponent(AActor* Actor)
{
	if (!Actor) return nullptr;

	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (APlayerState* PlayerState = Character->GetPlayerState<APlayerState>())
		{
			if (UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerState))
			{
				return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerState);
			}
		}
	}
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
}

