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

struct FPP_InputActiveComboChain
{
	FGameplayAbilitySpecHandle CurrentAbilityHandle;
	TSubclassOf<UGameplayAbility> NextAbilityClass = nullptr;
	FTimerHandle TimerHandle;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPPInputTag, FGameplayTag, InputTag);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="ActorComponent (PP Input)"))
class PROXYPREDICTION_API UPP_InputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_InputComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput")
	TObjectPtr<UPP_InputConfig> InputConfig = nullptr;

	UPROPERTY(BlueprintAssignable, Category="PPInput|Events")
	FOnPPInputTag OnInputPressed;

	UPROPERTY(BlueprintAssignable, Category="PPInput|Events")
	FOnPPInputTag OnInputReleased;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Held Activation")
	bool bRetryHeldAbilityActivation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Held Activation",
		meta=(EditCondition="bRetryHeldAbilityActivation", ClampMin="0.02", UIMin="0.02", Units="Seconds"))
	float HeldActivationRetryInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PPInput|Held Activation",
		meta=(EditCondition="bRetryHeldAbilityActivation"))
	FGameplayTagContainer HeldActivationRetryExcludedInputTags;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleNewPawn(APawn* NewPawn);
	void InstallForPawn(APawn* Pawn);
	void UninstallFromPawn();

	void AddMappingContextsForLocalPlayer() const;
	void RemoveMappingContextsForLocalPlayer() const;
	void BindActionsFromConfig();

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
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> InjectedEnhancedInputComponent = nullptr;

	FDelegateHandle NewPawnHandle;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	TMap<FGameplayAbilitySpecHandle, FPP_InputActiveComboChain> ActiveComboChains;
	TMap<FGameplayTag, FTimerHandle> HeldActivationRetryTimers;
	TSet<FGameplayTag> HeldActivationInputTags;

	TWeakObjectPtr<APlayerController> CachedPlayerController;
};

