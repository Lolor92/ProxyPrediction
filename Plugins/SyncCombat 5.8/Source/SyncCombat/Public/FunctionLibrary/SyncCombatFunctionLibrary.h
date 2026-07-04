// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SyncCombatFunctionLibrary.generated.h"


class UAbilitySystemComponent;

UCLASS()
class SYNCCOMBAT_API USyncCombatFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "SyncCombat|AbilitySystemComponent")
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Actor);
};
