#pragma once

#include "CoreMinimal.h"
#include <Components/ActorComponent.h>
#include "Data/SyncInputConfig.h"
#include "GameplayAbilitySpec.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "SyncInputComponent.generated.h"

class UAbilitySystemComponent;
class APlayerController;
class UEnhancedInputComponent;  
struct FInputActionValue;

struct FSyncInputActiveComboChain
{
	FGameplayAbilitySpecHandle CurrentAbilityHandle;
	TSubclassOf<UGameplayAbility> NextAbilityClass = nullptr;
	FTimerHandle TimerHandle;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSyncInputTag, FGameplayTag, InputTag);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), meta = (DisplayName = "ActorComponent (SyncInput)"))
class SYNCINPUT_API USyncInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USyncInputComponent();

	/** Data asset that holds mapping contexts and Action↔Tag pairs */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncInput")
	TObjectPtr<USyncInputConfig> InputConfig = nullptr;

	UPROPERTY(BlueprintAssignable, Category="SyncInput|Events")
	FOnSyncInputTag OnSyncInputPressed;

	UPROPERTY(BlueprintAssignable, Category="SyncInput|Events")
	FOnSyncInputTag OnSyncInputReleased;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncInput|Held Activation")
	bool bRetryHeldAbilityActivation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncInput|Held Activation",
		meta=(EditCondition="bRetryHeldAbilityActivation", ClampMin="0.02", UIMin="0.02", Units="Seconds"))
	float HeldActivationRetryInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncInput|Held Activation",
		meta=(EditCondition="bRetryHeldAbilityActivation"))
	FGameplayTagContainer HeldActivationRetryExcludedInputTags;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// lifecycle around possession changes
	void HandleNewPawn(class APawn* NewPawn);
	void InstallForPawn(APawn* Pawn);
	void UninstallFromPawn();

	// steps
	void AddMappingContextsForLocalPlayer() const;
	void RemoveMappingContextsForLocalPlayer() const;
	void BindActionsFromConfig();

	// helpers
	bool IsLocallyControlledOwner() const;
	class APlayerController* GetOwningPlayerController() const;
	bool DoesSpecMatchInputTag(const FGameplayAbilitySpec& Spec, const FGameplayTag& InputTag) const;
	bool HasAbilityForInputTag(FGameplayTag InputTag) const;
	bool CanLocallyActivateSpec(const FGameplayAbilitySpec& Spec) const;
	bool TryHandleAbilityPressed(FGameplayTag InputTag, bool bSendInputPressedEvent);
	bool TryActivateComboAbility(const FGameplayAbilitySpec& RequestedAbilitySpec, bool& bOutComboHandled);
	void UpdateComboChain(FGameplayAbilitySpecHandle StarterHandle, const FGameplayAbilitySpec& CurrentAbilitySpec);
	void ClearComboChain(FGameplayAbilitySpecHandle StarterHandle);
	void ClearAllComboChains();
	bool ShouldRetryHeldActivationForInputTag(FGameplayTag InputTag) const;
	void StartHeldActivationRetry(FGameplayTag InputTag);
	void StopHeldActivationRetry(FGameplayTag InputTag);
	void StopAllHeldActivationRetries();
	void RetryHeldActivation(FGameplayTag InputTag);

	// bound handlers
	void HandleActionPressed(FGameplayTag InputTag);
	void HandleActionReleased(FGameplayTag InputTag);
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

private:
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> InjectedEnhancedInputComponent = nullptr;

	FDelegateHandle NewPawnHandle;

	// (optional) cached ASC – only looked up on first use
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	TMap<FGameplayAbilitySpecHandle, FSyncInputActiveComboChain> ActiveComboChains;
	TMap<FGameplayTag, FTimerHandle> HeldActivationRetryTimers;
	TSet<FGameplayTag> HeldActivationInputTags;

	TWeakObjectPtr<APlayerController> CachedPlayerController;
};
