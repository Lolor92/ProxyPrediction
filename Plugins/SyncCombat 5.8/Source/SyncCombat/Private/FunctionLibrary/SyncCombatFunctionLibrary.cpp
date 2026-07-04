// Fill out your copyright notice in the Description page of Project Settings.


#include "FunctionLibrary/SyncCombatFunctionLibrary.h"
#include "GameFramework/Character.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/PlayerState.h"

UAbilitySystemComponent* USyncCombatFunctionLibrary::GetAbilitySystemComponent(AActor* Actor)
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
