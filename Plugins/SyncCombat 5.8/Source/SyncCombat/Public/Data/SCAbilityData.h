// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SCAbilityData.generated.h"

class UGameplayAbility;

USTRUCT(BlueprintType)
struct FSCAbilityGroup
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Group")
	FName GroupName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Group")
	bool bEnabled = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities")
	TArray<TSubclassOf<UGameplayAbility>> Abilities;
};

UCLASS(BlueprintType)
class SYNCCOMBAT_API USCAbilityData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities", meta = (TitleProperty = "GroupName"))
	TArray<FSCAbilityGroup> AbilityGroups;
};
