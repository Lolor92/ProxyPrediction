#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PP_CombatFunctionLibrary.generated.h"

class UAbilitySystemComponent;

UCLASS()
class PROXYPREDICTION_API UPP_CombatFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Combat|AbilitySystemComponent")
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Actor);
};
