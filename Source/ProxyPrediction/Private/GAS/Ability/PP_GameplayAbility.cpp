#include "GAS/Ability/PP_GameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "AbilityMotion/Component/PP_AbilityMotionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameplayEffect.h"

namespace
{
	void RefreshLocalCameraAfterAbilityYaw(ACharacter* Character)
	{
		if (!Character || !Character->IsLocallyControlled())
		{
			return;
		}

		if (USceneComponent* Root = Character->GetRootComponent())
		{
			Root->UpdateComponentToWorld();
			Root->UpdateChildTransforms();
		}

		TArray<USpringArmComponent*> SpringArms;
		Character->GetComponents(SpringArms);
		for (USpringArmComponent* SpringArm : SpringArms)
		{
			if (!SpringArm) continue;

			SpringArm->UpdateComponentToWorld();

			// SpringArm needs a zero-delta tick to recompute its socket from the snapped control yaw.
			SpringArm->TickComponent(0.f, LEVELTICK_All, nullptr);
			SpringArm->UpdateChildTransforms();
		}

		TArray<UCameraComponent*> Cameras;
		Character->GetComponents(Cameras);
		for (UCameraComponent* Camera : Cameras)
		{
			if (!Camera) continue;

			Camera->UpdateComponentToWorld();
		}
	}
}

UPP_GameplayAbility::UPP_GameplayAbility()
{
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	bServerRespectsRemoteAbilityCancellation = false;
	bRetriggerInstancedAbility = true;
}

bool UPP_GameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return CanInterruptAnimatingAbility(ActorInfo);
}

void UPP_GameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Retriggered instanced abilities must not leave handles or timers from the previous run.
	CleanupOwnedEffects();
	ActivationSequenceId = (ActivationSequenceId == MAX_uint32) ? 1u : (ActivationSequenceId + 1u);
	ResetComboWindow();
	ActivatedMontagePlayRate = ResolveActivationMontagePlayRate(ActorInfo);

	RotateAvatarToControllerYawOnActivate();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	InterruptOtherActiveAbilities();
	RemoveConfiguredGameplayEffectsOnActivate();
	ApplyAbilityLifetimeEffects();

	// Blueprint montage tasks start after this native activation returns; initialize once for next tick.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ThisClass::InitializeActivatedMontage, ActivationSequenceId));
	}
	OpenComboWindow();
}

void UPP_GameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	RestoreConfiguredMontagePlayRate();
	CleanupOwnedEffects();

	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (!Character->IsLocallyControlled())
		{
			if (UPP_AbilityMotionComponent* MotionComponent =
				Character->FindComponentByClass<UPP_AbilityMotionComponent>())
			{
				MotionComponent->ResetAbilityMotionState();
			}
		}
	}

	ResetComboWindow();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

FActiveGameplayEffectHandle UPP_GameplayAbility::ApplyOwnedEffect(
	const TSubclassOf<UGameplayEffect> EffectClass,
	const float EffectLevel) const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!EffectClass || !ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	const UGameplayEffect* EffectCDO = EffectClass->GetDefaultObject<UGameplayEffect>();
	return ApplyGameplayEffectToOwner(
		GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), EffectCDO,
		FMath::Max(0.0f, EffectLevel));
}

void UPP_GameplayAbility::ApplyAbilityLifetimeEffects()
{
	AbilityLifetimeEffectHandles.Reset();
	AbilityLifetimeEffectHandles.Reserve(AbilityLifetimeEffects.Num());

	for (const FPP_AbilityOwnedEffect& Effect : AbilityLifetimeEffects)
	{
		const FActiveGameplayEffectHandle Handle = ApplyOwnedEffect(Effect.GameplayEffectClass, Effect.EffectLevel);
		if (Handle.IsValid()) AbilityLifetimeEffectHandles.Add(Handle);
	}
}

void UPP_GameplayAbility::RemoveConfiguredGameplayEffectsOnActivate() const
{
	if (RemoveGameplayEffectsWithTagsOnActivate.IsEmpty()) return;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		// Local-predicted abilities clear responsive local state; authority confirms the same removal.
		ASC->RemoveActiveEffectsWithGrantedTags(RemoveGameplayEffectsWithTagsOnActivate);
	}
}

void UPP_GameplayAbility::InitializeActivatedMontage(const uint32 ExpectedActivationSequenceId)
{
	ApplyConfiguredMontagePlayRate(ExpectedActivationSequenceId);
	ScheduleMontageEffectWindows(ExpectedActivationSequenceId);
}

float UPP_GameplayAbility::ResolveActivationMontagePlayRate(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		ASC = GetAbilitySystemComponentFromActorInfo();
	}

	const UPP_GameplayAbility* PreviousAbility = Cast<UPP_GameplayAbility>(ASC ? ASC->GetAnimatingAbility() : nullptr);
	if (!PreviousAbility || !PreviousAbility->IsComboWindowOpen() ||
		!PreviousAbility->bUseComboNextAbilityMontagePlayRate)
	{
		return 1.f;
	}

	const UClass* ActivatingAbilityClass = GetClass();
	const UClass* ConfiguredNextComboAbilityClass = PreviousAbility->ComboAbilityClass.Get();
	if (!ActivatingAbilityClass || !ConfiguredNextComboAbilityClass ||
		!ActivatingAbilityClass->IsChildOf(ConfiguredNextComboAbilityClass))
	{
		return 1.f;
	}

	return FMath::Max(PreviousAbility->ComboNextAbilityMontagePlayRate, KINDA_SMALL_NUMBER);
}

void UPP_GameplayAbility::ApplyConfiguredMontagePlayRate(const uint32 ExpectedActivationSequenceId)
{
	if (ExpectedActivationSequenceId != ActivationSequenceId || !IsActive() ||
		FMath::IsNearlyEqual(ActivatedMontagePlayRate, 1.f))
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimMontage* Montage = GetCurrentMontage();
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	if (!Montage || !AnimInstance) return;

	AnimInstance->Montage_SetPlayRate(Montage, FMath::Max(ActivatedMontagePlayRate, KINDA_SMALL_NUMBER));
}

void UPP_GameplayAbility::RestoreConfiguredMontagePlayRate()
{
	if (FMath::IsNearlyEqual(ActivatedMontagePlayRate, 1.f))
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimMontage* Montage = GetCurrentMontage();
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	if (Montage && AnimInstance)
	{
		AnimInstance->Montage_SetPlayRate(Montage, 1.f);
	}

	ActivatedMontagePlayRate = 1.f;
}

void UPP_GameplayAbility::ScheduleMontageEffectWindows(const uint32 ExpectedActivationSequenceId)
{
	if (ExpectedActivationSequenceId != ActivationSequenceId || !IsActive() || MontageEffectWindows.IsEmpty()) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UAnimMontage* Montage = GetCurrentMontage();
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	UWorld* World = GetWorld();
	if (!Montage || !AnimInstance || !World) return;

	const float MontageLength = Montage->GetPlayLength();
	const float PlayRate = FMath::Abs(AnimInstance->Montage_GetPlayRate(Montage));
	if (MontageLength <= KINDA_SMALL_NUMBER || PlayRate <= KINDA_SMALL_NUMBER) return;

	const float CurrentPosition = AnimInstance->Montage_GetPosition(Montage);
	MontageEffectWindowRuntime.SetNum(MontageEffectWindows.Num());

	for (int32 Index = 0; Index < MontageEffectWindows.Num(); ++Index)
	{
		const FPP_AbilityMontageEffectWindow& Window = MontageEffectWindows[Index];
		if (!Window.GameplayEffectClass) continue;

		const float ApplyPercent = FMath::Clamp(Window.ApplyAtMontagePercent, 0.0f, 100.0f);
		const float RemovePercent = FMath::Clamp(
			FMath::Max(Window.RemoveAtMontagePercent, ApplyPercent), 0.0f, 100.0f);
		const float ApplyPosition = MontageLength * ApplyPercent / 100.0f;
		const float RemovePosition = MontageLength * RemovePercent / 100.0f;

		// If setup occurs after the entire window, do not flash a stale effect for one frame.
		if (CurrentPosition >= RemovePosition) continue;

		FPP_AbilityMontageEffectWindowRuntime& Runtime = MontageEffectWindowRuntime[Index];
		if (CurrentPosition >= ApplyPosition)
		{
			ApplyMontageEffectWindow(ExpectedActivationSequenceId, Index);
		}
		else
		{
			const float ApplyDelay = (ApplyPosition - CurrentPosition) / PlayRate;
			World->GetTimerManager().SetTimer(
				Runtime.ApplyTimer,
				FTimerDelegate::CreateUObject(this, &ThisClass::ApplyMontageEffectWindow,
					ExpectedActivationSequenceId, Index),
				FMath::Max(ApplyDelay, KINDA_SMALL_NUMBER), false);
		}

		const float RemoveDelay = (RemovePosition - CurrentPosition) / PlayRate;
		World->GetTimerManager().SetTimer(
			Runtime.RemoveTimer,
			FTimerDelegate::CreateUObject(this, &ThisClass::RemoveMontageEffectWindow,
				ExpectedActivationSequenceId, Index),
			FMath::Max(RemoveDelay, KINDA_SMALL_NUMBER), false);
	}
}

void UPP_GameplayAbility::ApplyMontageEffectWindow(
	const uint32 ExpectedActivationSequenceId,
	const int32 WindowIndex)
{
	if (ExpectedActivationSequenceId != ActivationSequenceId || !IsActive() ||
		!MontageEffectWindows.IsValidIndex(WindowIndex) || !MontageEffectWindowRuntime.IsValidIndex(WindowIndex))
	{
		return;
	}

	FPP_AbilityMontageEffectWindowRuntime& Runtime = MontageEffectWindowRuntime[WindowIndex];
	if (Runtime.EffectHandle.IsValid()) return;

	const FPP_AbilityMontageEffectWindow& Window = MontageEffectWindows[WindowIndex];
	Runtime.EffectHandle = ApplyOwnedEffect(Window.GameplayEffectClass, Window.EffectLevel);
}

void UPP_GameplayAbility::RemoveMontageEffectWindow(
	const uint32 ExpectedActivationSequenceId,
	const int32 WindowIndex)
{
	if (ExpectedActivationSequenceId != ActivationSequenceId ||
		!MontageEffectWindowRuntime.IsValidIndex(WindowIndex)) return;

	FPP_AbilityMontageEffectWindowRuntime& Runtime = MontageEffectWindowRuntime[WindowIndex];
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (Runtime.EffectHandle.IsValid()) ASC->RemoveActiveGameplayEffect(Runtime.EffectHandle);
	}
	Runtime.EffectHandle.Invalidate();
}

void UPP_GameplayAbility::CleanupOwnedEffects()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UWorld* World = GetWorld();

	for (FPP_AbilityMontageEffectWindowRuntime& Runtime : MontageEffectWindowRuntime)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(Runtime.ApplyTimer);
			World->GetTimerManager().ClearTimer(Runtime.RemoveTimer);
		}
		if (ASC && Runtime.EffectHandle.IsValid()) ASC->RemoveActiveGameplayEffect(Runtime.EffectHandle);
		Runtime.EffectHandle.Invalidate();
	}
	MontageEffectWindowRuntime.Reset();

	if (ASC)
	{
		for (const FActiveGameplayEffectHandle& Handle : AbilityLifetimeEffectHandles)
		{
			if (Handle.IsValid()) ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	AbilityLifetimeEffectHandles.Reset();
}

bool UPP_GameplayAbility::CanInterruptAnimatingAbility(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (MontageLockout.bBypassMontageLockout)
	{
		return true;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		ASC = GetAbilitySystemComponentFromActorInfo();
	}

	if (!ASC)
	{
		return true;
	}

	const UGameplayAbility* AnimatingAbility = ASC->GetAnimatingAbility();
	const UPP_GameplayAbility* AnimatingMotionAbility = Cast<UPP_GameplayAbility>(AnimatingAbility);
	if (!AnimatingMotionAbility)
	{
		return true;
	}

	if (!AnimatingMotionAbility->MontageLockout.bUseMontageProgressLockout ||
		!AnimatingMotionAbility->MontageLockout.bBlockAbilityActivationUntilUnlock)
	{
		return true;
	}

	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	}

	const UPP_AbilityMotionComponent* MotionComponent =
		Character ? Character->FindComponentByClass<UPP_AbilityMotionComponent>() : nullptr;
	if (MotionComponent && MotionComponent->GetAbilityMotionState().bCanBlendMontage)
	{
		return true;
	}

	const UAnimMontage* AnimatingMontage = AnimatingAbility->GetCurrentMontage();
	if (!AnimatingMontage)
	{
		return false;
	}

	const float MontageLength = AnimatingMontage->GetPlayLength();
	if (MontageLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}

	const float CurrentPosition = AnimInstance->Montage_GetPosition(AnimatingMontage);
	const float PlayedPercent = FMath::Clamp((CurrentPosition / MontageLength) * 100.f, 0.f, 100.f);
	float UnlockPercent = FMath::Clamp(
		AnimatingMotionAbility->MontageLockout.MontageProgressBeforeInterrupt, 0.f, 100.f);

	const UClass* ThisAbilityClass = GetClass();
	const UClass* ConfiguredComboAbilityClass = AnimatingMotionAbility->ComboAbilityClass.Get();
	const bool bActivatingConfiguredCombo =
		AnimatingMotionAbility->IsComboWindowOpen() &&
		ThisAbilityClass &&
		ConfiguredComboAbilityClass &&
		ThisAbilityClass->IsChildOf(ConfiguredComboAbilityClass);
	if (bActivatingConfiguredCombo && AnimatingMotionAbility->bUseComboMontageProgressBeforeInterrupt)
	{
		UnlockPercent = FMath::Clamp(
			AnimatingMotionAbility->ComboMontageProgressBeforeInterrupt, 0.f, 100.f);
	}

	return PlayedPercent >= UnlockPercent;
}

bool UPP_GameplayAbility::ShouldPauseRootMotionForCharacterCollision(const ACharacter* Character) const
{
	if (!bPauseRootMotionOnCharacterCollision || !Character) return false;

	UWorld* World = Character->GetWorld();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!World || !Capsule) return false;

	const float ProbeDistance = FMath::Max(0.f, RootMotionCharacterCollisionProbeDistance);
	if (ProbeDistance <= KINDA_SMALL_NUMBER) return false;

	FVector Forward = Character->GetActorForwardVector();
	Forward.Z = 0.f;
	if (!Forward.Normalize()) return false;

	const FVector Start = Capsule->GetComponentLocation();
	const FVector End = Start + Forward * ProbeDistance;
	const FCollisionShape CollisionShape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(),
		Capsule->GetScaledCapsuleHalfHeight());

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PPAbilityMotionCharacterCollisionProbe), false, Character);
	QueryParams.AddIgnoredActor(Character);

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FHitResult> Hits;
	const bool bFoundHit = World->SweepMultiByObjectType(
		Hits,
		Start,
		End,
		Capsule->GetComponentQuat(),
		ObjectQueryParams,
		CollisionShape,
		QueryParams);

	if (!bFoundHit) return false;

	const float ClampedAngleDegrees = FMath::Clamp(
		RootMotionCharacterCollisionForwardAngleDegrees,
		0.f,
		180.f);
	const float MinForwardDot = FMath::Cos(FMath::DegreesToRadians(ClampedAngleDegrees));

	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit && !Hit.bStartPenetrating) continue;

		const ACharacter* OtherCharacter = Cast<ACharacter>(Hit.GetActor());
		if (!OtherCharacter || OtherCharacter == Character) continue;

		const UPrimitiveComponent* OtherComp = Hit.GetComponent();
		if (OtherComp)
		{
			const bool bIsCapsule = OtherComp->IsA<UCapsuleComponent>();
			const bool bIsMesh = OtherComp->IsA<USkeletalMeshComponent>();
			if (!bIsCapsule && !bIsMesh) continue;
		}

		FVector HitPoint = Hit.ImpactPoint;
		if (Hit.bStartPenetrating || HitPoint.IsNearlyZero())
		{
			HitPoint = OtherCharacter->GetActorLocation();
		}

		FVector ToHit = HitPoint - Character->GetActorLocation();
		ToHit.Z = 0.f;
		if (!ToHit.Normalize()) continue;

		const float ForwardDot = FVector::DotProduct(Forward, ToHit);
		if (ForwardDot >= MinForwardDot)
		{
			return true;
		}
	}

	return false;
}

void UPP_GameplayAbility::OpenComboWindow()
{
	CloseComboWindow();

	if (!ComboAbilityClass || ComboWindowDuration <= 0.f) return;

	bComboWindowOpen = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ComboWindowTimerHandle,
			this,
			&ThisClass::CloseComboWindow,
			ComboWindowDuration,
			false);
	}
}

void UPP_GameplayAbility::CloseComboWindow()
{
	bComboWindowOpen = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboWindowTimerHandle);
	}
}

void UPP_GameplayAbility::ResetComboWindow()
{
	CloseComboWindow();
}

void UPP_GameplayAbility::InterruptOtherActiveAbilities() const
{
	if (!bInterruptOtherAbilitiesOnActivate) return;

	UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo();
	if (!AbilitySystemComponent) return;

	AbilitySystemComponent->CancelAllAbilities(const_cast<ThisClass*>(this));
}

void UPP_GameplayAbility::RotateAvatarToControllerYawOnActivate() const
{
	if (!bRotateToControllerYawOnActivate) return;

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info) return;

	ACharacter* Character = Cast<ACharacter>(Info->AvatarActor.Get());
	if (!Character) return;

	if (!Info->IsLocallyControlled() && !Info->IsNetAuthority()) return;

	AController* Controller = Info->PlayerController.IsValid()
		? Info->PlayerController.Get()
		: Character->GetController();
	if (!Controller) return;

	FRotator NewRot = Character->GetActorRotation();
	NewRot.Yaw = Controller->GetControlRotation().Yaw;
	Character->SetActorRotation(NewRot, ETeleportType::TeleportPhysics);
	RefreshLocalCameraAfterAbilityYaw(Character);
}

