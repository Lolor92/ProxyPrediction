#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "PP_InputConfig.generated.h"

class UInputMappingContext;

USTRUCT(BlueprintType)
struct FPP_InputMappingContextEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TObjectPtr<UInputMappingContext> InputMappingContext = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	int32 Priority = 0;
};

USTRUCT(BlueprintType)
struct FPP_InputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput", meta=(Categories="SyncInput"))
	FGameplayTag InputTag;
};

UCLASS()
class PROXYPREDICTION_API UPP_InputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PPInput")
	const UInputAction* FindInputActionByTag(const FGameplayTag& InputTag) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TArray<FPP_InputMappingContextEntry> MappingContexts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TArray<FPP_InputAction> InputActions;
};

