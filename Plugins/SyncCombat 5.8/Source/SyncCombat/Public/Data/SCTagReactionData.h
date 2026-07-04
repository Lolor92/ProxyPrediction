#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "SCTagReactionData.generated.h"

class UGameplayEffect;

UENUM(BlueprintType)
enum class ETagReactionPolicy : uint8
{
	OnAdd    UMETA(DisplayName="On Add"),
	OnRemove UMETA(DisplayName="On Remove"),
	Both     UMETA(DisplayName="On Both")
};

USTRUCT(BlueprintType)
struct FTagReactionAbility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(DisplayName="Ability Tag"))
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Delay (s)"))
	float DelaySeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FTagReactionEffects
{
	GENERATED_BODY()

	// Apply
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Apply", meta=(DisplayName="Apply"))
	TArray<TSubclassOf<UGameplayEffect>> Apply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Apply", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Apply Delay (s)"))
	float ApplyDelaySeconds = 0.f;

	// Remove
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Remove", meta=(DisplayName="Remove"))
	TArray<TSubclassOf<UGameplayEffect>> Remove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Remove", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Remove Delay (s)"))
	float RemoveDelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects|Remove", meta=(DisplayName="Remove Timer Key"))
	FName RemoveTimerKey;
};

USTRUCT(BlueprintType, meta=(DisplayName="Tag Reaction"))
struct FTagReactionBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trigger")
	FGameplayTag TriggerTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trigger")
	ETagReactionPolicy Policy = ETagReactionPolicy::OnAdd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(ShowOnlyInnerProperties))
	FTagReactionAbility Ability;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects", meta=(ShowOnlyInnerProperties))
	FTagReactionEffects Effects;
};

UCLASS()
class SYNCCOMBAT_API USCTagReactionData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncCombat|Tags", meta=(TitleProperty="TriggerTag"))
	TArray<FTagReactionBinding> Reactions;
};
