#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PP_ReactionData.generated.h"

class UAnimMontage;
class UGameplayEffect;

USTRUCT(BlueprintType, meta=(DisplayName="Predicted Reaction"))
struct FPP_ReactionDataEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FGameplayTag ReactionTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FName StartSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	float MinReplayInterval = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	bool bCancelActiveAbilityOnCleanHit = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction", meta=(TitleProperty="GameplayEffectClass"))
	TArray<TSubclassOf<UGameplayEffect>> TargetEffects;
};

UCLASS()
class PROXYPREDICTION_API UPP_ReactionData : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncPrediction|Reaction", meta=(TitleProperty="ReactionTag"))
	TArray<FPP_ReactionDataEntry> Reactions;

	UFUNCTION(BlueprintPure, Category="SyncPrediction|Reaction")
	const FPP_ReactionDataEntry& FindReactionChecked(FGameplayTag ReactionTag) const;

	UFUNCTION(BlueprintPure, Category="SyncPrediction|Reaction")
	bool FindReaction(FGameplayTag ReactionTag, FPP_ReactionDataEntry& OutReaction) const;

	const FPP_ReactionDataEntry* FindReactionPtr(FGameplayTag ReactionTag) const;
};
