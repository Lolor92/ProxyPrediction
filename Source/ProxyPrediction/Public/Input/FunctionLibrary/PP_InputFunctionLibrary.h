#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PP_InputFunctionLibrary.generated.h"

class UAbilitySystemComponent;

UCLASS()
class PROXYPREDICTION_API UPP_InputFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="PPInput|AbilitySystemComponent")
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Actor);
};

