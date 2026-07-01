#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "SyncInputConfig.generated.h"

class UInputMappingContext;

USTRUCT(BlueprintType)
struct FSyncInputMappingContextEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncInput")
	TObjectPtr<UInputMappingContext> InputMappingContext = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncInput")
	int32 Priority = 0;
};

USTRUCT(BlueprintType)
struct FSyncInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncInput")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncInput", meta=(Categories="SyncInput"))
	FGameplayTag InputTag;
};

UCLASS()
class SYNCINPUT_API USyncInputConfig : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintPure, Category="SyncInput")
	const UInputAction* FindInputActionByTag(const FGameplayTag& InputTag) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncInput")
	TArray<FSyncInputMappingContextEntry> MappingContexts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SyncInput")
	TArray<FSyncInputAction> SyncInputActions;
};
