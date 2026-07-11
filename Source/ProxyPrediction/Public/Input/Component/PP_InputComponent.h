#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "Input/Data/PP_InputConfig.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "PP_InputComponent.generated.h"

class APlayerController;
class UAbilitySystemComponent;
class UEnhancedInputComponent;
class UGameplayAbility;
struct FInputActionValue;

/** Runtime state for one active ability combo chain. */
struct FPP_InputActiveComboChain
{
	/** Ability currently playing in the chain. */
	FGameplayAbilitySpecHandle CurrentAbilityHandle;
	/** Ability class expected on the next press. */
	TSubclassOf<UGameplayAbility> NextAbilityClass = nullptr;
	/** Clears the chain when its combo window closes. */
	FTimerHandle TimerHandle;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPPInputTag, FGameplayTag, InputTag);

/**
 * Installs local Enhanced Input mappings and routes tagged actions to GAS.
 * Presses activate abilities or advance combos; held inputs may retry activation.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="ActorComponent (PP Input)"))
class PROXYPREDICTION_API UPP_InputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_InputComponent();

	/** Input mappings and tagged actions installed for the controlled pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput")
	TObjectPtr<UPP_InputConfig> InputConfig = nullptr;

	/** Broadcast when a tagged input starts. */
	UPROPERTY(BlueprintAssignable, Category="PPInput|Events")
	FOnPPInputTag OnInputPressed;

	/** Broadcast when a tagged input completes. */
	UPROPERTY(BlueprintAssignable, Category="PPInput|Events")
	FOnPPInputTag OnInputReleased;

	/** Recheck held abilities until they can activate or the input is released. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Held Activation")
	bool bRetryHeldAbilityActivation = true;

	/** Time between activation attempts while an input remains held. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Held Activation",
		meta=(EditCondition="bRetryHeldAbilityActivation", ClampMin="0.02", UIMin="0.02", Units="Seconds"))
	float HeldActivationRetryInterval = 0.1f;

	/** Only these input tags retry while held. An empty container disables held retries for every input. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Held Activation",
		meta=(EditCondition="bRetryHeldAbilityActivation"))
	FGameplayTagContainer HeldActivationRetryIncludedInputTags;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Pawn lifecycle: remove old bindings before installing the new local pawn.
	void HandleNewPawn(APawn* NewPawn);
	void InstallForPawn(APawn* Pawn);
	void UninstallFromPawn();

	// Enhanced Input setup from the selected input config.
	void AddMappingContextsForLocalPlayer() const;
	void RemoveMappingContextsForLocalPlayer() const;
	void BindActionsFromConfig();

	// Ability routing: match tag -> advance combo or activate matching GAS spec.
	bool IsLocallyControlledOwner() const;
	APlayerController* GetOwningPlayerController() const;
	bool DoesSpecMatchInputTag(const FGameplayAbilitySpec& Spec, const FGameplayTag& InputTag) const;
	bool HasAbilityForInputTag(FGameplayTag InputTag) const;
	bool CanLocallyActivateSpec(const FGameplayAbilitySpec& Spec) const;
	bool TryHandleAbilityPressed(FGameplayTag InputTag, bool bSendInputPressedEvent);
	bool TryActivateComboAbility(const FGameplayAbilitySpec& RequestedAbilitySpec, bool& bOutComboHandled);
	void UpdateComboChain(FGameplayAbilitySpecHandle StarterHandle, const FGameplayAbilitySpec& CurrentAbilitySpec);
	void ClearComboChain(FGameplayAbilitySpecHandle StarterHandle);
	void ClearAllComboChains();

	// Held activation retries stop as soon as the input is released.
	bool ShouldRetryHeldActivationForInputTag(FGameplayTag InputTag) const;
	void StartHeldActivationRetry(FGameplayTag InputTag);
	void StopHeldActivationRetry(FGameplayTag InputTag);
	void StopAllHeldActivationRetries();
	void RetryHeldActivation(FGameplayTag InputTag);

	void HandleActionPressed(FGameplayTag InputTag);
	void HandleActionReleased(FGameplayTag InputTag);
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

private:
	/** Input component pushed onto the local controller for these bindings. */
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> InjectedEnhancedInputComponent = nullptr;

	FDelegateHandle NewPawnHandle;

	/** Cached GAS component used for tagged ability activation. */
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	/** Combo chains keyed by the input's starter ability. */
	TMap<FGameplayAbilitySpecHandle, FPP_InputActiveComboChain> ActiveComboChains;
	/** Retry timers keyed by held input tag. */
	TMap<FGameplayTag, FTimerHandle> HeldActivationRetryTimers;
	/** Input tags that are currently held. */
	TSet<FGameplayTag> HeldActivationInputTags;

	/** Controller that owns the injected input component. */
	TWeakObjectPtr<APlayerController> CachedPlayerController;
};

