#include "Input/Component/PP_InputComponent.h"
#include "GAS/Ability/PP_GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Input/FunctionLibrary/PP_InputFunctionLibrary.h"
#include "InputActionValue.h"

namespace SyncInputTags
{
	static const FGameplayTag& Move()
	{
		static FGameplayTag T = FGameplayTag::RequestGameplayTag(TEXT("SyncInput.Move"));
		return T;
	}
	static const FGameplayTag& Look()
	{
		static FGameplayTag T = FGameplayTag::RequestGameplayTag(TEXT("SyncInput.Look"));
		return T;
	}
}

UPP_InputComponent::UPP_InputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPP_InputComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocallyControlledOwner()) return;

	if (APlayerController* PC = GetOwningPlayerController())
	{
		NewPawnHandle = PC->GetOnNewPawnNotifier().AddUObject(this, &UPP_InputComponent::HandleNewPawn);
		HandleNewPawn(PC->GetPawn());
	}
}

void UPP_InputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (NewPawnHandle.IsValid())
		{
			PC->GetOnNewPawnNotifier().Remove(NewPawnHandle);
			NewPawnHandle.Reset();
		}
	}

	UninstallFromPawn();
	Super::EndPlay(EndPlayReason);
}

void UPP_InputComponent::HandleNewPawn(APawn* NewPawn)
{
	// Controller possession changes replace the complete local input installation.
	UninstallFromPawn();
	if (!NewPawn || !IsLocallyControlledOwner()) return;

	InstallForPawn(NewPawn);
}

void UPP_InputComponent::InstallForPawn(APawn* Pawn)
{
	CachedPlayerController = GetOwningPlayerController();

	AddMappingContextsForLocalPlayer();

	if (APlayerController* PC = CachedPlayerController.Get())
	{
		if (!InjectedEnhancedInputComponent)
		{
			InjectedEnhancedInputComponent = NewObject<UEnhancedInputComponent>(
				PC, UEnhancedInputComponent::StaticClass(), TEXT("PPInput_InjectedInput"));
			InjectedEnhancedInputComponent->RegisterComponent();
			PC->PushInputComponent(InjectedEnhancedInputComponent);
		}
	}

	BindActionsFromConfig();
}

void UPP_InputComponent::UninstallFromPawn()
{
	RemoveMappingContextsForLocalPlayer();
	ClearAllComboChains();
	StopAllHeldActivationRetries();

	if (APlayerController* PC = CachedPlayerController.Get())
	{
		if (InjectedEnhancedInputComponent)
		{
			PC->PopInputComponent(InjectedEnhancedInputComponent);
			InjectedEnhancedInputComponent->DestroyComponent();
			InjectedEnhancedInputComponent = nullptr;
		}
	}
	CachedPlayerController = nullptr;
}

void UPP_InputComponent::AddMappingContextsForLocalPlayer() const
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			for (const FPP_InputMappingContextEntry& E : InputConfig->MappingContexts)
			{
				if (E.InputMappingContext)
				{
					Subsys->AddMappingContext(E.InputMappingContext, E.Priority);
				}
			}
		}
	}
	
}

void UPP_InputComponent::RemoveMappingContextsForLocalPlayer() const
{
	if (!InputConfig) return;

	const APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			for (const FPP_InputMappingContextEntry& E : InputConfig->MappingContexts)
			{
				if (E.InputMappingContext)
				{
					Subsys->RemoveMappingContext(E.InputMappingContext);
				}
			}
		}
	}
}

void UPP_InputComponent::BindActionsFromConfig()
{
	if (!InjectedEnhancedInputComponent) return;
	if (!InputConfig) return;

	if (InputConfig->InputActions.Num() == 0)
	{
	}

	for (const FPP_InputAction& Row : InputConfig->InputActions)
	{
		if (!Row.InputAction || !Row.InputTag.IsValid()) continue;

		// Move and Look feed the pawn directly; all other tags route through GAS.
		if (Row.InputTag.MatchesTagExact(SyncInputTags::Move()))
		{
			InjectedEnhancedInputComponent->BindAction(
				Row.InputAction, ETriggerEvent::Triggered, this, &UPP_InputComponent::Move);
			continue;
		}
		if (Row.InputTag.MatchesTagExact(SyncInputTags::Look()))
		{
			InjectedEnhancedInputComponent->BindAction(
				Row.InputAction, ETriggerEvent::Triggered, this, &UPP_InputComponent::Look);
			continue;
		}

		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Started,
			this, &UPP_InputComponent::HandleActionPressed, Row.InputTag);

		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Completed,
			this, &UPP_InputComponent::HandleActionReleased, Row.InputTag);
	}
}

bool UPP_InputComponent::IsLocallyControlledOwner() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	return PC && PC->IsLocalController();
}

APlayerController* UPP_InputComponent::GetOwningPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}

bool UPP_InputComponent::DoesSpecMatchInputTag(const FGameplayAbilitySpec& Spec, const FGameplayTag& InputTag) const
{
	return Spec.GetDynamicSpecSourceTags().HasTag(InputTag) ||
		(Spec.Ability && Spec.Ability->GetAssetTags().HasTag(InputTag));
}

bool UPP_InputComponent::HasAbilityForInputTag(FGameplayTag InputTag) const
{
	if (!AbilitySystemComponent) return false;

	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (DoesSpecMatchInputTag(Spec, InputTag))
		{
			return true;
		}
	}

	return false;
}

bool UPP_InputComponent::CanLocallyActivateSpec(const FGameplayAbilitySpec& Spec) const
{
	if (!AbilitySystemComponent || !Spec.Ability)
	{
		return false;
	}

	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystemComponent->AbilityActorInfo.Get();
	if (!ActorInfo)
	{
		return false;
	}

	return Spec.Ability->CanActivateAbility(Spec.Handle, ActorInfo);
}

void UPP_InputComponent::HandleActionPressed(FGameplayTag InputTag)
{
	OnInputPressed.Broadcast(InputTag);
	
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = UPP_InputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}
	if (!AbilitySystemComponent) return;

	// One press first advances an open combo, otherwise it activates the tagged spec.
	TryHandleAbilityPressed(InputTag, true);

	if (ShouldRetryHeldActivationForInputTag(InputTag) && HasAbilityForInputTag(InputTag))
	{
		StartHeldActivationRetry(InputTag);
	}
}

bool UPP_InputComponent::TryHandleAbilityPressed(FGameplayTag InputTag, bool bSendInputPressedEvent)
{
	if (!AbilitySystemComponent) return false;

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!DoesSpecMatchInputTag(Spec, InputTag)) continue;

		// An active chain owns this press even when its next ability cannot activate yet.
		bool bComboHandled = false;
		if (TryActivateComboAbility(Spec, bComboHandled))
		{
			return true;
		}

		if (bComboHandled)
		{
			return false;
		}

		if (!CanLocallyActivateSpec(Spec))
		{
			continue;
		}

		const bool bActivated = AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		if (bActivated)
		{
			UpdateComboChain(Spec.Handle, Spec);
		}

		if (bSendInputPressedEvent)
		{
			AbilitySystemComponent->AbilitySpecInputPressed(Spec);

			FPredictionKey PredictionKey;
			if (UGameplayAbility* PrimaryInstance = Spec.GetPrimaryInstance())
			{
				PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
			}
			AbilitySystemComponent->InvokeReplicatedEvent(
				EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, PredictionKey);
		}

		if (bActivated)
		{
			return true;
		}
	}

	return false;
}

void UPP_InputComponent::HandleActionReleased(FGameplayTag InputTag)
{
	OnInputReleased.Broadcast(InputTag);
	StopHeldActivationRetry(InputTag);
	
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = UPP_InputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}
	if (!AbilitySystemComponent) return;

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (DoesSpecMatchInputTag(Spec, InputTag))
		{
			if (Spec.IsActive())
			{
				AbilitySystemComponent->AbilitySpecInputReleased(Spec);

				FPredictionKey PredictionKey;
				if (UGameplayAbility* PrimaryInstance = Spec.GetPrimaryInstance())
				{
					PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
				}

				AbilitySystemComponent->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased,Spec.Handle,PredictionKey);
			}
		}
	}
}

bool UPP_InputComponent::TryActivateComboAbility(
	const FGameplayAbilitySpec& RequestedAbilitySpec,
	bool& bOutComboHandled)
{
	bOutComboHandled = false;
	if (!AbilitySystemComponent) return false;

	FPP_InputActiveComboChain* ComboChain = ActiveComboChains.Find(RequestedAbilitySpec.Handle);
	if (!ComboChain || !ComboChain->NextAbilityClass) return false;

	bOutComboHandled = true;

	for (FGameplayAbilitySpec& ComboSpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!ComboSpec.Ability || !ComboSpec.Ability->GetClass()->IsChildOf(ComboChain->NextAbilityClass)) continue;

		if (!CanLocallyActivateSpec(ComboSpec))
		{
			return false;
		}

		const bool bActivated = AbilitySystemComponent->TryActivateAbility(ComboSpec.Handle);
		if (bActivated)
		{
			UpdateComboChain(RequestedAbilitySpec.Handle, ComboSpec);
		}

		return true;
	}

	ClearComboChain(RequestedAbilitySpec.Handle);
	return true;
}

void UPP_InputComponent::UpdateComboChain(
	const FGameplayAbilitySpecHandle StarterHandle,
	const FGameplayAbilitySpec& CurrentAbilitySpec)
{
	UPP_GameplayAbility* CurrentAbility =
		Cast<UPP_GameplayAbility>(CurrentAbilitySpec.GetPrimaryInstance());
	if (!CurrentAbility)
	{
		CurrentAbility = Cast<UPP_GameplayAbility>(CurrentAbilitySpec.Ability);
	}

	if (!CurrentAbility || !CurrentAbility->GetComboAbilityClass() ||
		CurrentAbility->GetComboWindowDuration() <= 0.f)
	{
		ClearComboChain(StarterHandle);
		return;
	}

	// Keep the starter key so every later press advances the same chain.
	FPP_InputActiveComboChain& ComboChain = ActiveComboChains.FindOrAdd(StarterHandle);
	ComboChain.CurrentAbilityHandle = CurrentAbilitySpec.Handle;
	ComboChain.NextAbilityClass = CurrentAbility->GetComboAbilityClass();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboChain.TimerHandle);

		FTimerDelegate TimerDelegate =
			FTimerDelegate::CreateUObject(this, &ThisClass::ClearComboChain, StarterHandle);

		World->GetTimerManager().SetTimer(
			ComboChain.TimerHandle,
			TimerDelegate,
			CurrentAbility->GetComboWindowDuration(),
			false);
	}
}

void UPP_InputComponent::ClearComboChain(FGameplayAbilitySpecHandle StarterHandle)
{
	if (FPP_InputActiveComboChain* ComboChain = ActiveComboChains.Find(StarterHandle))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ComboChain->TimerHandle);
		}
	}

	ActiveComboChains.Remove(StarterHandle);
}

void UPP_InputComponent::ClearAllComboChains()
{
	UWorld* World = GetWorld();
	for (TPair<FGameplayAbilitySpecHandle, FPP_InputActiveComboChain>& ComboChainPair : ActiveComboChains)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(ComboChainPair.Value.TimerHandle);
		}
	}

	ActiveComboChains.Reset();
}

bool UPP_InputComponent::ShouldRetryHeldActivationForInputTag(FGameplayTag InputTag) const
{
	return bRetryHeldAbilityActivation &&
		InputTag.IsValid() &&
		InputTag.MatchesAny(HeldActivationRetryIncludedInputTags);
}

void UPP_InputComponent::StartHeldActivationRetry(FGameplayTag InputTag)
{
	if (!ShouldRetryHeldActivationForInputTag(InputTag)) return;

	HeldActivationInputTags.Add(InputTag);

	UWorld* World = GetWorld();
	if (!World) return;

	FTimerHandle& TimerHandle = HeldActivationRetryTimers.FindOrAdd(InputTag);
	if (World->GetTimerManager().IsTimerActive(TimerHandle)) return;

	const float RetryInterval = FMath::Max(0.02f, HeldActivationRetryInterval);
	FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(
		this,
		&ThisClass::RetryHeldActivation,
		InputTag);

	World->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, RetryInterval, true);
}

void UPP_InputComponent::StopHeldActivationRetry(FGameplayTag InputTag)
{
	HeldActivationInputTags.Remove(InputTag);

	if (FTimerHandle* TimerHandle = HeldActivationRetryTimers.Find(InputTag))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(*TimerHandle);
		}
	}

	HeldActivationRetryTimers.Remove(InputTag);
}

void UPP_InputComponent::StopAllHeldActivationRetries()
{
	UWorld* World = GetWorld();
	for (TPair<FGameplayTag, FTimerHandle>& RetryTimerPair : HeldActivationRetryTimers)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(RetryTimerPair.Value);
		}
	}

	HeldActivationRetryTimers.Reset();
	HeldActivationInputTags.Reset();
}

void UPP_InputComponent::RetryHeldActivation(FGameplayTag InputTag)
{
	if (!HeldActivationInputTags.Contains(InputTag) || !ShouldRetryHeldActivationForInputTag(InputTag))
	{
		StopHeldActivationRetry(InputTag);
		return;
	}

	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = UPP_InputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}

	if (!AbilitySystemComponent)
	{
		StopHeldActivationRetry(InputTag);
		return;
	}

	// Retries activation without repeating the GAS InputPressed event every interval.
	TryHandleAbilityPressed(InputTag, false);
}

void UPP_InputComponent::Move(const FInputActionValue& Value)
{
	// Retrieve 2D input vector (X: right/left, Y: forward/backward)
	const FVector2D InputVector = Value.Get<FVector2D>();

	// Get Yaw rotation from controller
	const APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController) return;

	const FRotator YawRotation(0.f, PlayerController->GetControlRotation().Yaw, 0.f);

	// Calculate forward and right directions based on Yaw rotation
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// Add movement input to pawn if valid
	if (APawn* ControlledPawn = Cast<APawn>(GetOwner()))
	{
		ControlledPawn->AddMovementInput(Forward, InputVector.Y);
		ControlledPawn->AddMovementInput(Right,   InputVector.X);
	}
}

void UPP_InputComponent::Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();

	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		PlayerController->AddYawInput(LookVector.X);
		PlayerController->AddPitchInput(LookVector.Y);
	}
}


