#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PP_CombatAbilityData.generated.h"

class UGameplayAbility;

USTRUCT(BlueprintType)
struct FPP_CombatAbilityGroup
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
class PROXYPREDICTION_API UPP_CombatAbilityData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities", meta=(TitleProperty="GroupName"))
	TArray<FPP_CombatAbilityGroup> AbilityGroups;
};
