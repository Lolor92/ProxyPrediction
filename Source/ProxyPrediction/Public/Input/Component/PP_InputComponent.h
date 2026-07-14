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

enum class EPP_AbilityPressResult : uint8
{
	NotHandled,
	Handled,
	Activated,
	BlockedByMontage,
	BlockedOther
};

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

	/** Keeps only the most recent ability press made during a montage lockout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Ability Buffer")
	bool bBufferAbilityInputDuringMontageLockout = true;

	/** Maximum time a montage-locked press remains buffered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Ability Buffer",
		meta=(EditCondition="bBufferAbilityInputDuringMontageLockout", ClampMin="0.0", UIMin="0.0", Units="Seconds"))
	float AbilityInputBufferDuration = 0.5f;

	/** How often the single buffered press checks the montage interrupt point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Ability Buffer",
		meta=(EditCondition="bBufferAbilityInputDuringMontageLockout", ClampMin="0.02", UIMin="0.02", Units="Seconds"))
	float AbilityInputBufferRetryInterval = 0.02f;

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
	bool IsSpecBlockedByMontageLockout(const FGameplayAbilitySpec& Spec) const;
	EPP_AbilityPressResult TryHandleAbilityPressed(FGameplayTag InputTag, bool bSendInputPressedEvent);
	EPP_AbilityPressResult TryActivateComboAbility(
		const FGameplayAbilitySpec& RequestedAbilitySpec, bool& bOutComboHandled);
	void SendInputPressedToActiveSpec(FGameplayAbilitySpec& Spec) const;
	void UpdateComboChain(FGameplayAbilitySpecHandle StarterHandle, const FGameplayAbilitySpec& CurrentAbilitySpec);
	void ClearComboChain(FGameplayAbilitySpecHandle StarterHandle);
	void ClearAllComboChains();

	// Held activation retries stop as soon as the input is released.
	bool ShouldRetryHeldActivationForInputTag(FGameplayTag InputTag) const;
	void StartHeldActivationRetry(FGameplayTag InputTag);
	void StopHeldActivationRetry(FGameplayTag InputTag);
	void StopAllHeldActivationRetries();
	void RetryHeldActivation(FGameplayTag InputTag);
	void BufferAbilityInput(FGameplayTag InputTag);
	void RetryBufferedAbilityInput();
	void ClearBufferedAbilityInput();
	void TrackPendingSelfRetrigger(FGameplayAbilitySpecHandle AbilityHandle);
	void ResolvePendingSelfRetrigger(
		FGameplayAbilitySpecHandle AbilityHandle, int16 PredictionKey, bool bRejected);
	void RetryBufferedAbilityInputAfterPredictionResolution();

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
	/** Last-input-wins buffer used only while another ability owns the montage lockout. */
	FGameplayTag BufferedAbilityInputTag;
	FTimerHandle BufferedAbilityInputTimer;
	double BufferedAbilityInputExpirationTime = 0.0;
	/**
	 * One outstanding locally predicted self-retrigger per ability spec. A later press may stay in
	 * the shared input buffer, but cannot generate another prediction key until this one resolves.
	 */
	TMap<FGameplayAbilitySpecHandle, int16> PendingSelfRetriggerPredictionKeys;
	/** Defers rejection retries until GAS has finished ending the rejected ability instance. */
	FTimerHandle SelfRetriggerResolutionRetryTimer;
	/** Prevents two Enhanced Input callbacks in one frame from both starting an ability. */
	uint64 LastAbilityActivationFrame = MAX_uint64;

	/** Controller that owns the injected input component. */
	TWeakObjectPtr<APlayerController> CachedPlayerController;
};

