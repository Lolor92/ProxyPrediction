#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PP_InputFunctionLibrary.generated.h"

class UAbilitySystemComponent;

/** Blueprint helpers shared by the project input path. */
UCLASS()
class PROXYPREDICTION_API UPP_InputFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Finds GAS on the character's PlayerState first, then on the actor itself. */
	UFUNCTION(BlueprintCallable, Category="PPInput|AbilitySystemComponent")
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Actor);
};

