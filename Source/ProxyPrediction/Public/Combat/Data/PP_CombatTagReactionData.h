#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PP_CombatTagReactionData.generated.h"

class UGameplayEffect;

UENUM(BlueprintType)
enum class EPP_CombatTagReactionPolicy : uint8
{
	OnAdd UMETA(DisplayName="On Add"),
	OnRemove UMETA(DisplayName="On Remove"),
	Both UMETA(DisplayName="On Both")
};

USTRUCT(BlueprintType)
struct FPP_CombatTagReactionAbility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(DisplayName="Ability Tag"))
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Delay (s)"))
	float DelaySeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FPP_CombatTagReactionEffects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Apply", meta=(DisplayName="Apply"))
	TArray<TSubclassOf<UGameplayEffect>> Apply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Apply", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Apply Delay (s)"))
	float ApplyDelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Remove", meta=(DisplayName="Remove"))
	TArray<TSubclassOf<UGameplayEffect>> Remove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Remove", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Remove Delay (s)"))
	float RemoveDelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Remove", meta=(DisplayName="Remove Timer Key"))
	FName RemoveTimerKey;
};

USTRUCT(BlueprintType, meta=(DisplayName="Combat Tag Reaction"))
struct FPP_CombatTagReactionBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trigger")
	FGameplayTag TriggerTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trigger")
	EPP_CombatTagReactionPolicy Policy = EPP_CombatTagReactionPolicy::OnAdd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(ShowOnlyInnerProperties))
	FPP_CombatTagReactionAbility Ability;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects", meta=(ShowOnlyInnerProperties))
	FPP_CombatTagReactionEffects Effects;
};

UCLASS()
class PROXYPREDICTION_API UPP_CombatTagReactionData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Tags", meta=(TitleProperty="TriggerTag"))
	TArray<FPP_CombatTagReactionBinding> Reactions;
};
