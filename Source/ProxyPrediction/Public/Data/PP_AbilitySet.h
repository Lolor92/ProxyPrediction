#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpec.h"
#include "PP_AbilitySet.generated.h"

class UGameplayAbility;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FPP_AbilitySet_GameplayAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities")
	TSubclassOf<UGameplayAbility> Ability = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities")
	int32 AbilityLevel = 1;
};

USTRUCT(BlueprintType)
struct FPP_AbilitySet_GrantedHandles
{
	GENERATED_BODY()

public:
	void AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle);
	void ClearAbilities(UAbilitySystemComponent* ASC);

private:
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;
};

UCLASS()
class PROXYPREDICTION_API UPP_AbilitySet : public UDataAsset
{
	GENERATED_BODY()
	
public:
	void GiveToAbilitySystem(UAbilitySystemComponent* ASC, FPP_AbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject = nullptr) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sync Prediction|Abilities", meta=(TitleProperty="Ability"))
	TArray<FPP_AbilitySet_GameplayAbility> GrantedGameplayAbilities;
};
