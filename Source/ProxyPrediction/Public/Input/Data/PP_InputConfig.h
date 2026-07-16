#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "PP_InputConfig.generated.h"

class UInputMappingContext;

/** Mapping context installed for the locally controlled pawn. */
USTRUCT(BlueprintType)
struct FPP_InputMappingContextEntry
{
	GENERATED_BODY()

	/** Enhanced Input mapping context to install. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TObjectPtr<UInputMappingContext> InputMappingContext = nullptr;

	/** Mapping priority passed to the local player subsystem. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	int32 Priority = 0;
};

/** Associates an Enhanced Input action with the gameplay tag used by PP input. */
USTRUCT(BlueprintType)
struct FPP_InputAction
{
	GENERATED_BODY()

	/** Enhanced Input action that produces the input event. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TObjectPtr<UInputAction> InputAction = nullptr;

	/** Tag used to route the action to movement, look, or a gameplay ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput", meta=(Categories="SyncInput"))
	FGameplayTag InputTag;

	/** Event that routes this action to GAS. Use Triggered for delayed triggers such as Repeated Tap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	ETriggerEvent AbilityActivationEvent = ETriggerEvent::Started;
};

/** Data asset that defines mapping contexts and tag-based input actions. */
UCLASS()
class PROXYPREDICTION_API UPP_InputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Returns the action assigned to an exact input tag, or null when unassigned. */
	UFUNCTION(BlueprintPure, Category="PPInput")
	const UInputAction* FindInputActionByTag(const FGameplayTag& InputTag) const;

	/** Mapping contexts installed while this config controls the local pawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TArray<FPP_InputMappingContextEntry> MappingContexts;

	/** Actions routed by their input tags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PPInput")
	TArray<FPP_InputAction> InputActions;
};

