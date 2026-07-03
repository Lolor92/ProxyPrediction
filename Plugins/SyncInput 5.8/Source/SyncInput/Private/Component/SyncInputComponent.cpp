#include "Component/SyncInputComponent.h"
#include "Ability/SyncAbilityMotionGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "FunctionLibrary/SyncInputFunctionLibrary.h"
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

USyncInputComponent::USyncInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USyncInputComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocallyControlledOwner()) return;

	if (APlayerController* PC = GetOwningPlayerController())
	{
		NewPawnHandle = PC->GetOnNewPawnNotifier().AddUObject(this, &USyncInputComponent::HandleNewPawn);
		HandleNewPawn(PC->GetPawn());
	}
}

void USyncInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

void USyncInputComponent::HandleNewPawn(APawn* NewPawn)
{
	UninstallFromPawn();
	if (!NewPawn || !IsLocallyControlledOwner()) return;

	InstallForPawn(NewPawn);
}

void USyncInputComponent::InstallForPawn(APawn* Pawn)
{
	CachedPlayerController = GetOwningPlayerController();

	AddMappingContextsForLocalPlayer();

	if (APlayerController* PC = CachedPlayerController.Get())
	{
		if (!InjectedEnhancedInputComponent)
		{
			InjectedEnhancedInputComponent = NewObject<UEnhancedInputComponent>(
				PC, UEnhancedInputComponent::StaticClass(), TEXT("SyncInput_InjectedInput"));
			InjectedEnhancedInputComponent->RegisterComponent();
			PC->PushInputComponent(InjectedEnhancedInputComponent);
		}
	}

	BindActionsFromConfig();
}

void USyncInputComponent::UninstallFromPawn()
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

void USyncInputComponent::AddMappingContextsForLocalPlayer() const
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			for (const FSyncInputMappingContextEntry& E : InputConfig->MappingContexts)
			{
				if (E.InputMappingContext)
				{
					Subsys->AddMappingContext(E.InputMappingContext, E.Priority);
				}
			}
		}
	}
	
}

void USyncInputComponent::RemoveMappingContextsForLocalPlayer() const
{
	if (!InputConfig) return;

	const APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			for (const FSyncInputMappingContextEntry& E : InputConfig->MappingContexts)
			{
				if (E.InputMappingContext)
				{
					Subsys->RemoveMappingContext(E.InputMappingContext);
				}
			}
		}
	}
}

void USyncInputComponent::BindActionsFromConfig()
{
	if (!InjectedEnhancedInputComponent) return;
	if (!InputConfig) return;

	if (InputConfig->SyncInputActions.Num() == 0)
	{
	}

	for (const FSyncInputAction& Row : InputConfig->SyncInputActions)
	{
		if (!Row.InputAction || !Row.InputTag.IsValid()) continue;

		// Special-case Move/Look: bind to axis handlers
		if (Row.InputTag.MatchesTagExact(SyncInputTags::Move()))
		{
			InjectedEnhancedInputComponent->BindAction(
				Row.InputAction, ETriggerEvent::Triggered, this, &USyncInputComponent::Move);
			continue;
		}
		if (Row.InputTag.MatchesTagExact(SyncInputTags::Look()))
		{
			InjectedEnhancedInputComponent->BindAction(
				Row.InputAction, ETriggerEvent::Triggered, this, &USyncInputComponent::Look);
			continue;
		}

		// Everything else forwards to GAS via the tag
		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Started,
			this, &USyncInputComponent::HandleActionPressed, Row.InputTag);

		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Completed,
			this, &USyncInputComponent::HandleActionReleased, Row.InputTag);
	}
}

bool USyncInputComponent::IsLocallyControlledOwner() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	return PC && PC->IsLocalController();
}

APlayerController* USyncInputComponent::GetOwningPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}

bool USyncInputComponent::DoesSpecMatchInputTag(const FGameplayAbilitySpec& Spec, const FGameplayTag& InputTag) const
{
	return Spec.GetDynamicSpecSourceTags().HasTag(InputTag) ||
		(Spec.Ability && Spec.Ability->GetAssetTags().HasTag(InputTag));
}

bool USyncInputComponent::HasAbilityForInputTag(FGameplayTag InputTag) const
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

bool USyncInputComponent::CanLocallyActivateSpec(const FGameplayAbilitySpec& Spec) const
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

void USyncInputComponent::HandleActionPressed(FGameplayTag InputTag)
{
	OnSyncInputPressed.Broadcast(InputTag);
	
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = USyncInputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}
	if (!AbilitySystemComponent) return;

	TryHandleAbilityPressed(InputTag, true);

	if (ShouldRetryHeldActivationForInputTag(InputTag) && HasAbilityForInputTag(InputTag))
	{
		StartHeldActivationRetry(InputTag);
	}
}

bool USyncInputComponent::TryHandleAbilityPressed(FGameplayTag InputTag, bool bSendInputPressedEvent)
{
	if (!AbilitySystemComponent) return false;

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!DoesSpecMatchInputTag(Spec, InputTag)) continue;

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

void USyncInputComponent::HandleActionReleased(FGameplayTag InputTag)
{
	OnSyncInputReleased.Broadcast(InputTag);
	StopHeldActivationRetry(InputTag);
	
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = USyncInputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
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

bool USyncInputComponent::TryActivateComboAbility(
	const FGameplayAbilitySpec& RequestedAbilitySpec,
	bool& bOutComboHandled)
{
	bOutComboHandled = false;
	if (!AbilitySystemComponent) return false;

	FSyncInputActiveComboChain* ComboChain = ActiveComboChains.Find(RequestedAbilitySpec.Handle);
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

void USyncInputComponent::UpdateComboChain(
	const FGameplayAbilitySpecHandle StarterHandle,
	const FGameplayAbilitySpec& CurrentAbilitySpec)
{
	USyncAbilityMotionGameplayAbility* CurrentAbility =
		Cast<USyncAbilityMotionGameplayAbility>(CurrentAbilitySpec.GetPrimaryInstance());
	if (!CurrentAbility)
	{
		CurrentAbility = Cast<USyncAbilityMotionGameplayAbility>(CurrentAbilitySpec.Ability);
	}

	if (!CurrentAbility || !CurrentAbility->GetComboAbilityClass() ||
		CurrentAbility->GetComboWindowDuration() <= 0.f)
	{
		ClearComboChain(StarterHandle);
		return;
	}

	FSyncInputActiveComboChain& ComboChain = ActiveComboChains.FindOrAdd(StarterHandle);
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

void USyncInputComponent::ClearComboChain(FGameplayAbilitySpecHandle StarterHandle)
{
	if (FSyncInputActiveComboChain* ComboChain = ActiveComboChains.Find(StarterHandle))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ComboChain->TimerHandle);
		}
	}

	ActiveComboChains.Remove(StarterHandle);
}

void USyncInputComponent::ClearAllComboChains()
{
	UWorld* World = GetWorld();
	for (TPair<FGameplayAbilitySpecHandle, FSyncInputActiveComboChain>& ComboChainPair : ActiveComboChains)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(ComboChainPair.Value.TimerHandle);
		}
	}

	ActiveComboChains.Reset();
}

bool USyncInputComponent::ShouldRetryHeldActivationForInputTag(FGameplayTag InputTag) const
{
	return bRetryHeldAbilityActivation &&
		InputTag.IsValid() &&
		!HeldActivationRetryExcludedInputTags.HasTag(InputTag);
}

void USyncInputComponent::StartHeldActivationRetry(FGameplayTag InputTag)
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

void USyncInputComponent::StopHeldActivationRetry(FGameplayTag InputTag)
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

void USyncInputComponent::StopAllHeldActivationRetries()
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

void USyncInputComponent::RetryHeldActivation(FGameplayTag InputTag)
{
	if (!HeldActivationInputTags.Contains(InputTag) || !ShouldRetryHeldActivationForInputTag(InputTag))
	{
		StopHeldActivationRetry(InputTag);
		return;
	}

	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = USyncInputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}

	if (!AbilitySystemComponent)
	{
		StopHeldActivationRetry(InputTag);
		return;
	}

	TryHandleAbilityPressed(InputTag, false);
}

void USyncInputComponent::Move(const FInputActionValue& Value)
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

void USyncInputComponent::Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();

	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		PlayerController->AddYawInput(LookVector.X);
		PlayerController->AddPitchInput(LookVector.Y);
	}
}
