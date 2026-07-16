#include "Input/Component/PP_InputComponent.h"
#include "GAS/Ability/PP_GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayPrediction.h"
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
	ClearBufferedAbilityInput();
	PendingSelfRetriggerPredictionKeys.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SelfRetriggerResolutionRetryTimer);
	}
	SelfRetriggerResolutionRetryTimer.Invalidate();
	LastAbilityActivationFrame = MAX_uint64;

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
	AbilitySystemComponent = nullptr;
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
			Row.InputAction, Row.AbilityActivationEvent,
			this, &UPP_InputComponent::HandleActionPressed, Row.InputTag);

		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Completed,
			this, &UPP_InputComponent::HandleActionReleased, Row.InputTag);

		// Chorded actions can transition from Started/Triggered to Canceled when the modifier
		// is released while the direction key remains down. Treat that as a physical release too,
		// otherwise the held-activation retry timer can keep firing after the chord has ended.
		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Canceled,
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

bool UPP_InputComponent::IsSpecBlockedByMontageLockout(const FGameplayAbilitySpec& Spec) const
{
	if (!AbilitySystemComponent || !Spec.Ability) return false;

	const UPP_GameplayAbility* Ability = Cast<UPP_GameplayAbility>(Spec.Ability);
	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystemComponent->AbilityActorInfo.Get();
	return Ability && ActorInfo && !Ability->CanActivateDuringCurrentMontage(ActorInfo);
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
	// A montage-locked press replaces the single buffered input (last press wins).
	const EPP_AbilityPressResult Result = TryHandleAbilityPressed(InputTag, true);
	if (Result == EPP_AbilityPressResult::BlockedByMontage)
	{
		BufferAbilityInput(InputTag);
	}
	else if (Result == EPP_AbilityPressResult::Activated &&
		BufferedAbilityInputTag.MatchesTagExact(InputTag))
	{
		ClearBufferedAbilityInput();
	}

	if (ShouldRetryHeldActivationForInputTag(InputTag) && HasAbilityForInputTag(InputTag))
	{
		StartHeldActivationRetry(InputTag);
	}
}

EPP_AbilityPressResult UPP_InputComponent::TryHandleAbilityPressed(
	FGameplayTag InputTag,
	bool bSendInputPressedEvent)
{
	if (!AbilitySystemComponent) return EPP_AbilityPressResult::NotHandled;

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!DoesSpecMatchInputTag(Spec, InputTag)) continue;
		if (bSendInputPressedEvent)
		{
			// Preserve the physical state even when activation is montage-buffered.
			// Held retries will then send the correct pressed bit when they activate.
			Spec.InputPressed = true;
		}

		// An active chain owns this press even when its next ability cannot activate yet.
		bool bComboHandled = false;
		const EPP_AbilityPressResult ComboResult = TryActivateComboAbility(Spec, bComboHandled);
		if (bComboHandled)
		{
			return ComboResult;
		}

		if (PendingSelfRetriggerPredictionKeys.Contains(Spec.Handle))
		{
			// The montage can finish locally before its prediction key catches up at very high
			// latency. Gate by spec handle rather than active state so that brief inactive gap
			// cannot create a second speculative generation either.
			return EPP_AbilityPressResult::BlockedByMontage;
		}

		// A progress-gated ability may intentionally replace itself once its montage unlocks.
		// Before that point this press uses the same single-slot buffer as a different locked
		// ability. Once unlocked, TryActivateAbility lets GAS end and retrigger the instance.
		if (Spec.IsActive())
		{
			const UPP_GameplayAbility* ActiveAbility = Cast<UPP_GameplayAbility>(Spec.Ability);
			const FGameplayAbilityActorInfo* ActorInfo = AbilitySystemComponent->AbilityActorInfo.Get();
			if (ActiveAbility && ActorInfo && ActiveAbility->IsMontageGatedSelfRetrigger(ActorInfo))
			{
				if (!ActiveAbility->CanActivateDuringCurrentMontage(ActorInfo) ||
					LastAbilityActivationFrame == GFrameCounter)
				{
					return EPP_AbilityPressResult::BlockedByMontage;
				}

				if (!CanLocallyActivateSpec(Spec))
				{
					return IsSpecBlockedByMontageLockout(Spec)
						? EPP_AbilityPressResult::BlockedByMontage
						: EPP_AbilityPressResult::BlockedOther;
				}

				if (AbilitySystemComponent->TryActivateAbility(Spec.Handle))
				{
					LastAbilityActivationFrame = GFrameCounter;
					TrackPendingSelfRetrigger(Spec.Handle);
					UpdateComboChain(Spec.Handle, Spec);
					return EPP_AbilityPressResult::Activated;
				}

				return IsSpecBlockedByMontageLockout(Spec)
					? EPP_AbilityPressResult::BlockedByMontage
					: EPP_AbilityPressResult::BlockedOther;
			}

			// Preserve ordinary active-ability input tasks for abilities that did not opt into
			// montage-progress self replacement.
			if (bSendInputPressedEvent) SendInputPressedToActiveSpec(Spec);
			return EPP_AbilityPressResult::Handled;
		}

		// Enhanced Input may dispatch two Started callbacks in one frame. Only the
		// first is allowed to start an ability; the newer press is buffered.
		if (LastAbilityActivationFrame == GFrameCounter)
		{
			return EPP_AbilityPressResult::BlockedByMontage;
		}

		if (!CanLocallyActivateSpec(Spec))
		{
			return IsSpecBlockedByMontageLockout(Spec)
				? EPP_AbilityPressResult::BlockedByMontage
				: EPP_AbilityPressResult::BlockedOther;
		}

		// Do not emit a generic InputPressed event for a new activation;
		// TryActivateAbility already creates and sends its prediction key.
		const bool bActivated = AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		if (bActivated)
		{
			LastAbilityActivationFrame = GFrameCounter;
			UpdateComboChain(Spec.Handle, Spec);
			return EPP_AbilityPressResult::Activated;
		}

		return IsSpecBlockedByMontageLockout(Spec)
			? EPP_AbilityPressResult::BlockedByMontage
			: EPP_AbilityPressResult::BlockedOther;
	}

	return EPP_AbilityPressResult::NotHandled;
}

void UPP_InputComponent::SendInputPressedToActiveSpec(FGameplayAbilitySpec& Spec) const
{
	if (!AbilitySystemComponent || !Spec.IsActive()) return;

	AbilitySystemComponent->AbilitySpecInputPressed(Spec);

	FPredictionKey PredictionKey;
	if (UGameplayAbility* PrimaryInstance = Spec.GetPrimaryInstance())
	{
		PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
	}

	AbilitySystemComponent->InvokeReplicatedEvent(
		EAbilityGenericReplicatedEvent::InputPressed,
		Spec.Handle,
		PredictionKey);
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
			const bool bWasActive = Spec.IsActive();
			AbilitySystemComponent->AbilitySpecInputReleased(Spec);
			if (bWasActive)
			{
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

EPP_AbilityPressResult UPP_InputComponent::TryActivateComboAbility(
	const FGameplayAbilitySpec& RequestedAbilitySpec,
	bool& bOutComboHandled)
{
	bOutComboHandled = false;
	if (!AbilitySystemComponent) return EPP_AbilityPressResult::NotHandled;

	FPP_InputActiveComboChain* ComboChain = ActiveComboChains.Find(RequestedAbilitySpec.Handle);
	if (!ComboChain || !ComboChain->NextAbilityClass) return EPP_AbilityPressResult::NotHandled;

	bOutComboHandled = true;

	for (FGameplayAbilitySpec& ComboSpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!ComboSpec.Ability || !ComboSpec.Ability->GetClass()->IsChildOf(ComboChain->NextAbilityClass)) continue;

		if (ComboSpec.IsActive())
		{
			return EPP_AbilityPressResult::Handled;
		}

		if (LastAbilityActivationFrame == GFrameCounter)
		{
			return EPP_AbilityPressResult::BlockedByMontage;
		}

		if (!CanLocallyActivateSpec(ComboSpec))
		{
			return IsSpecBlockedByMontageLockout(ComboSpec)
				? EPP_AbilityPressResult::BlockedByMontage
				: EPP_AbilityPressResult::BlockedOther;
		}

		ComboSpec.InputPressed = RequestedAbilitySpec.InputPressed;
		const bool bActivated = AbilitySystemComponent->TryActivateAbility(ComboSpec.Handle);
		if (bActivated)
		{
			LastAbilityActivationFrame = GFrameCounter;
			UpdateComboChain(RequestedAbilitySpec.Handle, ComboSpec);
			return EPP_AbilityPressResult::Activated;
		}

		return IsSpecBlockedByMontageLockout(ComboSpec)
			? EPP_AbilityPressResult::BlockedByMontage
			: EPP_AbilityPressResult::BlockedOther;
	}

	ClearComboChain(RequestedAbilitySpec.Handle);
	return EPP_AbilityPressResult::Handled;
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
	const EPP_AbilityPressResult Result = TryHandleAbilityPressed(InputTag, false);
	if (Result == EPP_AbilityPressResult::BlockedByMontage)
	{
		BufferAbilityInput(InputTag);
	}
}

void UPP_InputComponent::BufferAbilityInput(FGameplayTag InputTag)
{
	if (!bBufferAbilityInputDuringMontageLockout || !InputTag.IsValid() || AbilityInputBufferDuration <= 0.f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// There is intentionally one slot. Repeated or simultaneous presses cannot
	// build a queue of prediction RPCs; the most recent requested skill wins.
	BufferedAbilityInputTag = InputTag;
	BufferedAbilityInputExpirationTime =
		static_cast<double>(World->GetTimeSeconds()) + static_cast<double>(AbilityInputBufferDuration);

	if (!World->GetTimerManager().IsTimerActive(BufferedAbilityInputTimer))
	{
		World->GetTimerManager().SetTimer(
			BufferedAbilityInputTimer,
			this,
			&ThisClass::RetryBufferedAbilityInput,
			FMath::Max(0.02f, AbilityInputBufferRetryInterval),
			true);
	}
}

void UPP_InputComponent::RetryBufferedAbilityInput()
{
	UWorld* World = GetWorld();
	if (!World || !BufferedAbilityInputTag.IsValid() ||
		static_cast<double>(World->GetTimeSeconds()) > BufferedAbilityInputExpirationTime)
	{
		ClearBufferedAbilityInput();
		return;
	}

	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = UPP_InputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}
	if (!AbilitySystemComponent)
	{
		ClearBufferedAbilityInput();
		return;
	}

	const EPP_AbilityPressResult Result = TryHandleAbilityPressed(BufferedAbilityInputTag, false);
	if (Result != EPP_AbilityPressResult::BlockedByMontage)
	{
		// Cooldown, cost, and tag failures are not montage buffering conditions.
		// They clear the request instead of unexpectedly firing it much later.
		ClearBufferedAbilityInput();
	}
}

void UPP_InputComponent::ClearBufferedAbilityInput()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedAbilityInputTimer);
	}

	BufferedAbilityInputTimer.Invalidate();
	BufferedAbilityInputTag = FGameplayTag();
	BufferedAbilityInputExpirationTime = 0.0;
}

void UPP_InputComponent::TrackPendingSelfRetrigger(const FGameplayAbilitySpecHandle AbilityHandle)
{
	if (!AbilitySystemComponent || !AbilityHandle.IsValid()) return;

	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystemComponent->AbilityActorInfo.Get();
	if (!ActorInfo || !ActorInfo->IsLocallyControlled() || ActorInfo->IsNetAuthority()) return;

	const FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromHandle(AbilityHandle);
	const UGameplayAbility* AbilityInstance = Spec ? Spec->GetPrimaryInstance() : nullptr;
	if (!AbilityInstance) return;

	FPredictionKey PredictionKey =
		AbilityInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
	if (!PredictionKey.IsLocalClientKey()) return;

	PendingSelfRetriggerPredictionKeys.Add(AbilityHandle, PredictionKey.Current);
	PredictionKey.NewRejectedDelegate().BindUObject(
		this, &ThisClass::ResolvePendingSelfRetrigger,
		AbilityHandle, PredictionKey.Current, true);
	PredictionKey.NewCaughtUpDelegate().BindUObject(
		this, &ThisClass::ResolvePendingSelfRetrigger,
		AbilityHandle, PredictionKey.Current, false);

}

void UPP_InputComponent::ResolvePendingSelfRetrigger(
	const FGameplayAbilitySpecHandle AbilityHandle,
	const int16 PredictionKey,
	const bool /*bRejected*/)
{
	const int16* PendingKey = PendingSelfRetriggerPredictionKeys.Find(AbilityHandle);
	if (!PendingKey || *PendingKey != PredictionKey) return;

	PendingSelfRetriggerPredictionKeys.Remove(AbilityHandle);
	if (!BufferedAbilityInputTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(SelfRetriggerResolutionRetryTimer)) return;

	// Rejection delegates run before GAS calls EndAbility on the rejected instance. Waiting one
	// tick prevents the buffered retry from recursively reusing an instance that is still ending.
	SelfRetriggerResolutionRetryTimer = World->GetTimerManager().SetTimerForNextTick(
		this, &ThisClass::RetryBufferedAbilityInputAfterPredictionResolution);
}

void UPP_InputComponent::RetryBufferedAbilityInputAfterPredictionResolution()
{
	SelfRetriggerResolutionRetryTimer.Invalidate();
	RetryBufferedAbilityInput();
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


