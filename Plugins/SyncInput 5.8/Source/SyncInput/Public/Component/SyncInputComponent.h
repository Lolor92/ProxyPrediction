#pragma once

#include "CoreMinimal.h"
#include <Components/ActorComponent.h>
#include "Data/SyncInputConfig.h"
#include "InputActionValue.h"
#include "SyncInputComponent.generated.h"

class UAbilitySystemComponent;
class APlayerController;
class UEnhancedInputComponent;  
struct FInputActionValue;

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

	TWeakObjectPtr<APlayerController> CachedPlayerController;
};
