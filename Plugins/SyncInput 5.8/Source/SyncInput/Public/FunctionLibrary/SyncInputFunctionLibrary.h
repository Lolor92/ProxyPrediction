

#pragma once

#include "CoreMinimal.h"
#include <Kismet/BlueprintFunctionLibrary.h>
#include "SyncInputFunctionLibrary.generated.h"

class UAbilitySystemComponent;

UCLASS()
class SYNCINPUT_API USyncInputFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "SyncInput|AbilitySystemComponent")
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Actor);
};
