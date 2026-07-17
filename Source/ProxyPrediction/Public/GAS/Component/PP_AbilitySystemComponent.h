#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GAS/Data/PP_AbilitySet.h"
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

	/**
	 * Activates an ability after snapshotting the autonomous owner's current controller yaw.
	 * Root-motion abilities that rotate on activation can then use the identical starting yaw
	 * on the predicting client and server instead of the server sampling a later control rotation.
	 */
	bool TryActivateAbilityWithSyncedFacing(FGameplayAbilitySpecHandle AbilityHandle,
		bool bAllowRemoteActivation = true);

	/** Tag-based equivalent used by delayed reaction test activations. */
	bool TryActivateAbilitiesByTagWithSyncedFacing(const FGameplayTagContainer& GameplayTagContainer,
		bool bAllowRemoteActivation = true);

	/** Consumes the prepared yaw for this activation, if one is still current. */
	bool ConsumePreparedAbilityActivationYaw(FGameplayAbilitySpecHandle AbilityHandle, float& OutYaw);
	
protected:
	virtual void InternalServerTryActivateAbility(FGameplayAbilitySpecHandle AbilityToActivate,
		bool bInputPressed, const FPredictionKey& PredictionKey,
		const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<TObjectPtr<UPP_AbilitySet>> AbilitySetsToGrant;
	
private:
	struct FPendingAbilityActivationYaw
	{
		float Yaw = 0.f;
		double PreparedWorldTime = 0.0;
	};

	bool PrepareAbilityActivationYaw(FGameplayAbilitySpecHandle AbilityHandle);
	void DiscardPreparedAbilityActivationYaw(FGameplayAbilitySpecHandle AbilityHandle, bool bNotifyServer);

	UFUNCTION(Server, Reliable)
	void ServerPrepareAbilityActivationYaw(FGameplayAbilitySpecHandle AbilityHandle, float ActivationYaw);

	UFUNCTION(Server, Reliable)
	void ServerDiscardPreparedAbilityActivationYaw(FGameplayAbilitySpecHandle AbilityHandle);

	UPROPERTY()
	FPP_AbilitySet_GrantedHandles GrantedHandles;

	TMap<FGameplayAbilitySpecHandle, FPendingAbilityActivationYaw> PendingAbilityActivationYaws;
	TMap<FGameplayAbilitySpecHandle, FPendingAbilityActivationYaw> ServerActivationYaws;

	bool bAbilitySetsGranted = false;
};

