#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Data/PP_AbilitySet.h"
#include "PP_AbilitySystemComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_AbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	UPP_AbilitySystemComponent();
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UFUNCTION(BlueprintCallable)
	void GrantAbilitySets();

	UFUNCTION(BlueprintCallable)
	void RemoveGrantedAbilitySets();
	
protected:
	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<TObjectPtr<UPP_AbilitySet>> AbilitySetsToGrant;
	
private:
	UPROPERTY()
	FPP_AbilitySet_GrantedHandles GrantedHandles;

	bool bAbilitySetsGranted = false;
};
