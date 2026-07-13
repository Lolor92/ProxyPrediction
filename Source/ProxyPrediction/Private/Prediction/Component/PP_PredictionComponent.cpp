#include "Prediction/Component/PP_PredictionComponent.h"

#include "Combat/Component/PP_CombatComponent.h"
#include "TimerManager.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "AbilityMotion/Component/PP_AbilityMotionComponent.h"
#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"


namespace
{
	template <typename TEnum>
	bool PP_IsValidReactionEnumValue(TEnum Value)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Value));
	}

	static void PP_PrepareCharacterForReactionRootMotion
	(AActor* Actor, const TCHAR* Role, int32 PredictionId,
	 FGameplayTag ReactionTag)
	{
		ACharacter* Character = Cast<ACharacter>(Actor);
		if (!Character) return;

		UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
		if (!MovementComponent) return;

		MovementComponent->StopMovementImmediately();
		MovementComponent->ClearAccumulatedForces();
		Character->ConsumeMovementInputVector();
		MovementComponent->Velocity = FVector::ZeroVector;
		MovementComponent->UpdateComponentVelocity();
	}

	static void PP_SetOwnerMontageTrackCorrectionSuppressed
	(UCharacterMovementComponent* MovementComponent,
	 bool bSuppressed,
	 const FPP_ReactionPredictionContext& Context,
	 AActor* TargetActor, FGameplayTag ReactionTag,
	 const TCHAR* Reason)
	{
		UPP_CharacterMovementComponent* SyncMoveComponent =
			Cast<UPP_CharacterMovementComponent>(MovementComponent);
		if (!SyncMoveComponent) return;

		SyncMoveComponent->SetIgnoreServerRootMotionMontageTrackCorrection(bSuppressed);
	}
}

UPP_PredictionComponent::UPP_PredictionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPP_PredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAllClientReactionMovementInterpolations();
	Super::EndPlay(EndPlayReason);
}

bool UPP_PredictionComponent::ConsumeServerReactionRequestBudget()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	const double NowSeconds = World->GetTimeSeconds();
	if (ServerReactionRequestWindowStartSeconds < 0.0 ||
		NowSeconds - ServerReactionRequestWindowStartSeconds >= 1.0)
	{
		ServerReactionRequestWindowStartSeconds = NowSeconds;
		ServerReactionRequestsInCurrentWindow = 0;

		for (auto It = LastServerReactionTimeByTarget.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	if (MaxServerReactionRequestsPerSecond > 0 &&
		ServerReactionRequestsInCurrentWindow >= MaxServerReactionRequestsPerSecond)
	{
		return false;
	}

	++ServerReactionRequestsInCurrentWindow;
	return true;
}

bool UPP_PredictionComponent::ValidateServerReactionRequest(
	AActor* TargetActor,
	const FPP_ReactionDataEntry& Reaction,
	const FPP_ReactionTransformSettings& TransformSettings)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !TargetActor || TargetActor == OwnerActor || !World ||
		TargetActor->GetWorld() != World || TargetActor->IsActorBeingDestroyed() ||
		!Reaction.Montage || !Cast<ACharacter>(TargetActor))
	{
		return false;
	}

	if (MaxServerReactionRequestDistance > 0.0f &&
		FVector::DistSquared(OwnerActor->GetActorLocation(), TargetActor->GetActorLocation()) >
		FMath::Square(MaxServerReactionRequestDistance))
	{
		return false;
	}

	if (TransformSettings.bUseServerStartTransform)
	{
		return false;
	}

	const FPP_ReactionMovementSettings& Movement = TransformSettings.MovementSettings;
	const FPP_ReactionRotationSettings& Rotation = TransformSettings.RotationSettings;

	const bool bEnumsValid =
		PP_IsValidReactionEnumValue(Movement.MoveDirection) &&
		PP_IsValidReactionEnumValue(Movement.Recipient) &&
		PP_IsValidReactionEnumValue(Movement.ReferenceActorSource) &&
		PP_IsValidReactionEnumValue(Movement.LateralOffsetMode) &&
		PP_IsValidReactionEnumValue(Movement.TeleportType) &&
		PP_IsValidReactionEnumValue(Rotation.RotationDirection) &&
		PP_IsValidReactionEnumValue(Rotation.Recipient) &&
		PP_IsValidReactionEnumValue(Rotation.ReferenceActorSource) &&
		PP_IsValidReactionEnumValue(Rotation.TeleportType);
	if (!bEnumsValid || !FMath::IsFinite(Movement.MoveDistance) || !FMath::IsFinite(Movement.LateralOffset) ||
		!FMath::IsFinite(Movement.ClientInterpolationSpeed) || Movement.MoveDistance < 0.0f ||
		Movement.ClientInterpolationSpeed < 0.0f ||
		(MaxServerReactionMoveDistance > 0.0f && Movement.MoveDistance > MaxServerReactionMoveDistance) ||
		(MaxServerReactionLateralOffset > 0.0f &&
			FMath::Abs(Movement.LateralOffset) > MaxServerReactionLateralOffset) ||
		Rotation.DirectionToFace.ContainsNaN() || Rotation.ReferenceFacingOverride.ContainsNaN())
	{
		return false;
	}

	const double NowSeconds = World->GetTimeSeconds();
	if (const double* LastAcceptedTime = LastServerReactionTimeByTarget.Find(TargetActor))
	{
		if (NowSeconds - *LastAcceptedTime < FMath::Max(0.0f, Reaction.MinReplayInterval))
		{
			return false;
		}
	}

	LastServerReactionTimeByTarget.Add(TargetActor, NowSeconds);
	return true;
}

bool UPP_PredictionComponent::CanServerAcceptPredictedReaction_Implementation(
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	const FPP_ReactionTransformSettings& TransformSettings) const
{
	return TargetActor != nullptr && ReactionTag.IsValid();
}

EPP_ReactionDefenseOutcome UPP_PredictionComponent::ResolveDefenseOutcome(
	AActor* TargetActor,
	const FPP_ReactionDefenseSettings& DefenseSettings) const
{
	const UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	if (!TargetCombat) return EPP_ReactionDefenseOutcome::None;

	const bool bBlocked = DefenseSettings.Block.bBlockable &&
		TargetCombat->CanBlockAttackFrom(GetOwner(), DefenseSettings.Block.BlockAngleDegrees);
	if (bBlocked && TargetCombat->IsParryingActive()) return EPP_ReactionDefenseOutcome::Parried;
	if (DefenseSettings.Dodge.bDodgeable && TargetCombat->IsDodgingActive()) return EPP_ReactionDefenseOutcome::Dodged;
	if (bBlocked) return EPP_ReactionDefenseOutcome::Blocked;
	if (DefenseSettings.SuperArmor.RequiredSuperArmor != EPP_SuperArmorLevel::None &&
		TargetCombat->GetSuperArmorLevel() >= DefenseSettings.SuperArmor.RequiredSuperArmor)
	{
		return EPP_ReactionDefenseOutcome::SuperArmored;
	}

	return EPP_ReactionDefenseOutcome::None;
}

bool UPP_PredictionComponent::ShouldApplyReactionTransform(
	const EPP_ReactionDefenseOutcome Outcome,
	const FPP_ReactionDefenseSettings& DefenseSettings)
{
	if (Outcome == EPP_ReactionDefenseOutcome::None) return true;
	if (Outcome == EPP_ReactionDefenseOutcome::Blocked || Outcome == EPP_ReactionDefenseOutcome::Parried)
	{
		return DefenseSettings.Block.bAllowMovementWhenBlocked ||
			DefenseSettings.Block.bAllowRotationWhenBlocked;
	}
	return false;
}

void UPP_PredictionComponent::ApplyDefenseOutcomeEffects(
	AActor* TargetActor,
	const EPP_ReactionDefenseOutcome Outcome) const
{
	UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	if (!TargetCombat) return;

	switch (Outcome)
	{
	case EPP_ReactionDefenseOutcome::Parried:
		// Parry is a specialized successful block, so both configured responses fire.
		TargetCombat->ApplySuccessfulBlockEffects(GetOwner());
		TargetCombat->ApplySuccessfulParryEffects(GetOwner());
		break;
	case EPP_ReactionDefenseOutcome::Blocked:
		TargetCombat->ApplySuccessfulBlockEffects(GetOwner());
		break;
	case EPP_ReactionDefenseOutcome::Dodged:
		TargetCombat->ApplySuccessfulDodgeEffects(GetOwner());
		break;
	case EPP_ReactionDefenseOutcome::SuperArmored:
		TargetCombat->ApplySuccessfulSuperArmorEffects(GetOwner());
		break;
	default:
		break;
	}
}

bool UPP_PredictionComponent::ShouldApplyDamage(
	AActor* TargetActor,
	const EPP_ReactionDefenseOutcome Outcome,
	const FPP_ReactionDamageSettings& DamageSettings) const
{
	if (DamageSettings.DamageEffects.IsEmpty()) return false;
	if (Outcome == EPP_ReactionDefenseOutcome::Parried &&
		(!DamageSettings.bApplyDamageWhenParried || !DamageSettings.bApplyDamageWhenBlocked)) return false;
	if (Outcome == EPP_ReactionDefenseOutcome::Blocked && !DamageSettings.bApplyDamageWhenBlocked) return false;
	if (Outcome == EPP_ReactionDefenseOutcome::Dodged && !DamageSettings.bApplyDamageWhenDodged) return false;

	const UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	return !TargetCombat || !TargetCombat->DoesCurrentSuperArmorIgnoreDamage();
}

void UPP_PredictionComponent::ApplyDamageEffects(
	AActor* TargetActor,
	const FPP_ReactionDamageSettings& DamageSettings) const
{
	AActor* InstigatorActor = GetOwner();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActor(InstigatorActor);
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	if (!InstigatorActor || !InstigatorActor->HasAuthority() || !SourceASC || !TargetASC) return;

	for (const FPP_ReactionDamageEffect& DamageEffect : DamageSettings.DamageEffects)
	{
		if (!DamageEffect.GameplayEffectClass) continue;

		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(InstigatorActor, InstigatorActor);
		Context.AddSourceObject(this);

		const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			DamageEffect.GameplayEffectClass, DamageEffect.EffectLevel, Context);
		if (Spec.IsValid())
		{
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		}
	}
}

bool UPP_PredictionComponent::PlayPredictedReactionOnTargetProxy
(AActor* TargetActor, FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings,
 FPP_ReactionDefenseSettings DefenseSettings, FPP_ReactionDamageSettings DamageSettings)
{
	// Local prediction: validate -> mark -> transform -> montage -> server RPC.
	if (!ReactionData || !ReactionTag.IsValid()) return false;

	const FPP_ReactionTransformSettings RequestedTransformSettings = TransformSettings;
	const EPP_ReactionDefenseOutcome PredictedDefenseOutcome =
		ResolveDefenseOutcome(TargetActor, DefenseSettings);
	if (PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::Blocked ||
		PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::Parried)
	{
		// A successful guard normally keeps both characters in place unless this attack opts in.
		if (!DefenseSettings.Block.bAllowMovementWhenBlocked)
		{
			TransformSettings.MovementSettings.MoveDirection = EPP_ReactionMoveDirection::None;
		}
		if (!DefenseSettings.Block.bAllowRotationWhenBlocked)
		{
			TransformSettings.RotationSettings.RotationDirection = EPP_ReactionRotationDirection::None;
		}
	}

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return false;

	if (!CanPlayPredictedReactionOnTargetProxy(TargetActor, Reaction)) return false;

	const FPP_ReactionPredictionContext Context = MakeReactionPredictionContext();
	const bool bPredictCleanReaction = PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::None;

	AddPendingPredictedReaction(Context, TargetActor, ReactionTag, bPredictCleanReaction);

	const float StartPosition = GetReactionStartPosition(Reaction);

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		TransformSettings.RotationSettings.bUseReferenceFacingOverride = true;
		TransformSettings.RotationSettings.ReferenceFacingOverride =
			FRotator(0.f, OwnerPawn->GetControlRotation().Yaw, 0.f);
	}

	if (bPredictCleanReaction)
	{
		ApplyLatestOlderProxyPendingFinalReactionCorrection(Context, TargetActor, TEXT("PredictedPreflight"));
		ApplyReactionTransform(GetOwner(), TargetActor, TransformSettings);

		if (!PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true))
		{
			ConsumePendingPredictedReaction(Context, TargetActor);
			return false;
		}
	}

	if (const UWorld* World = GetWorld())
	{
		LastReactionTimeByTarget.Add(TargetActor, World->GetTimeSeconds());
	}

	// Send the clean-hit request; the server independently selects the defensive outcome.
	ServerConfirmPredictedReaction(Context, TargetActor, ReactionTag, RequestedTransformSettings,
		DefenseSettings, DamageSettings);

	return true;
}

void UPP_PredictionComponent::ServerConfirmPredictedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 AActor* TargetActor,
 FGameplayTag ReactionTag,
 FPP_ReactionTransformSettings TransformSettings,
 FPP_ReactionDefenseSettings DefenseSettings,
 FPP_ReactionDamageSettings DamageSettings)
{
	ProcessServerConfirmedReaction(Context, TargetActor, ReactionTag, TransformSettings,
		DefenseSettings, DamageSettings);
}

void UPP_PredictionComponent::ProcessServerConfirmedReaction(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	const FPP_ReactionTransformSettings& TransformSettings,
	const FPP_ReactionDefenseSettings& DefenseSettings,
	const FPP_ReactionDamageSettings& DamageSettings)
{
	// Authority repeats the transform, applies effects, then publishes start and finish.
	AActor* OwnerActor = GetOwner();


	if (!OwnerActor || !OwnerActor->HasAuthority()) return;

	if (!Context.IsValid() || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return;
	if (!ConsumeServerReactionRequestBudget()) return;
	if (!PP_IsValidReactionEnumValue(DefenseSettings.SuperArmor.RequiredSuperArmor) ||
		!FMath::IsFinite(DefenseSettings.Block.BlockAngleDegrees) ||
		DefenseSettings.Block.BlockAngleDegrees < 0.0f || DefenseSettings.Block.BlockAngleDegrees > 180.0f ||
		DamageSettings.DamageEffects.Num() > 16)
	{
		return;
	}
	for (const FPP_ReactionDamageEffect& DamageEffect : DamageSettings.DamageEffects)
	{
		if (!FMath::IsFinite(DamageEffect.EffectLevel) || DamageEffect.EffectLevel < 0.0f) return;
	}

	// Resolve defense from authoritative target tags and transforms.
	const EPP_ReactionDefenseOutcome DefenseOutcome = ResolveDefenseOutcome(TargetActor, DefenseSettings);

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;
	if (!CanServerAcceptPredictedReaction(TargetActor, ReactionTag, TransformSettings)) return;

	FPP_ReactionTransformSettings ResolvedTransformSettings = TransformSettings;
	if (DefenseOutcome == EPP_ReactionDefenseOutcome::Blocked ||
		DefenseOutcome == EPP_ReactionDefenseOutcome::Parried)
	{
		if (!DefenseSettings.Block.bAllowMovementWhenBlocked)
		{
			ResolvedTransformSettings.MovementSettings.MoveDirection = EPP_ReactionMoveDirection::None;
		}
		if (!DefenseSettings.Block.bAllowRotationWhenBlocked)
		{
			ResolvedTransformSettings.RotationSettings.RotationDirection = EPP_ReactionRotationDirection::None;
		}
	}

	// Rebuild facing from the server controller to match the local prediction rule.
	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		ResolvedTransformSettings.RotationSettings.bUseReferenceFacingOverride = true;
		ResolvedTransformSettings.RotationSettings.ReferenceFacingOverride =
			FRotator(0.0f, OwnerPawn->GetControlRotation().Yaw, 0.0f);
	}

	if (!ValidateServerReactionRequest(TargetActor, Reaction, ResolvedTransformSettings)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);


	const bool bCleanHit = DefenseOutcome == EPP_ReactionDefenseOutcome::None;
	if (bCleanHit || ShouldApplyReactionTransform(DefenseOutcome, DefenseSettings))
	{
		ApplyReactionTransform(OwnerActor, TargetActor, ResolvedTransformSettings);
	}

	const FVector ServerStartLocation = TargetActor->GetActorLocation();
	const FRotator ServerStartRotation = TargetActor->GetActorRotation();

	if (ShouldApplyDamage(TargetActor, DefenseOutcome, DamageSettings))
	{
		ApplyDamageEffects(TargetActor, DamageSettings);
	}


	if (bCleanHit)
	{
		ApplyTargetEffects(OwnerActor, TargetActor, Reaction);
		PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	}
	else
	{
		ApplyDefenseOutcomeEffects(TargetActor, DefenseOutcome);
		MulticastPlayConfirmedReaction(Context, TargetActor, ReactionTag,
			ServerStartLocation, ServerStartRotation, 0.0f, false);
		return;
	}


	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);
		TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FPP_ReactionPredictionContext CapturedContext = Context;
		const FGameplayTag CapturedReactionTag = ReactionTag;


		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(TimerHandle, [WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
		                                  {
			                                  UPP_PredictionComponent* StrongThis = WeakThis.Get();
			                                  AActor* StrongTarget = WeakTarget.Get();
			                                  if (!StrongThis || !StrongTarget) return;

			                                  const FVector ServerFinalLocation = StrongTarget->GetActorLocation();
			                                  const FRotator ServerFinalRotation = StrongTarget->GetActorRotation();
			                                  StrongThis->ClientFinishPredictedProxyReaction(
				                                  CapturedContext, StrongTarget, CapturedReactionTag,
				                                  ServerFinalLocation, ServerFinalRotation);

			                                  if (UPP_PredictionComponent* TargetPredictionComponent =
				                                  StrongTarget->FindComponentByClass<UPP_PredictionComponent>())
			                                  {
				                                  TargetPredictionComponent->ClientFinishOwnerConfirmedReaction(
					                                  CapturedContext, CapturedReactionTag,
					                                  ServerFinalLocation, ServerFinalRotation);
			                                  }
		                                  },
		                                  RemainingDuration,
		                                  false);
	}

	if (UPP_PredictionComponent* TargetPredictionComponent =
		TargetActor->FindComponentByClass<UPP_PredictionComponent>())
	{
		TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(
			Context, ReactionTag, ServerStartLocation, ServerStartRotation,
			ResolvedTransformSettings.MovementSettings.ClientInterpolationSpeed);
	}

	MulticastPlayConfirmedReaction(Context, TargetActor, ReactionTag,
	                               ServerStartLocation, ServerStartRotation,
	                               ResolvedTransformSettings.MovementSettings.ClientInterpolationSpeed, true);
}

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 AActor* TargetActor,
 FGameplayTag ReactionTag,
 FVector ServerStartLocation,
 FRotator ServerStartRotation,
 float ClientInterpolationSpeed,
 bool bPlayReaction)
{
	// The predicting client consumes its marker; other proxies start from server state.
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || OwnerActor->HasAuthority()) return;

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();

	FGameplayTag PredictedReactionTag;
	bool bPlayedLocally = false;
	const bool bConsumedPendingPrediction =
		ConsumePendingPredictedReaction(Context, TargetActor, &PredictedReactionTag, &bPlayedLocally);
	if (!bPlayReaction)
	{
		if (bConsumedPendingPrediction && bPlayedLocally)
		{
			// Local proxy state was stale: cancel the clean montage and restore server state.
			FPP_ReactionDataEntry PredictedReaction;
			if (ReactionData->FindReaction(PredictedReactionTag, PredictedReaction) && PredictedReaction.Montage)
			{
				if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
				{
					if (UAnimInstance* AnimInstance = TargetCharacter->GetMesh()->GetAnimInstance())
					{
						AnimInstance->Montage_Stop(0.1f, PredictedReaction.Montage);
					}
				}
			}
			TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
			FPP_ReactionMovementSettings CorrectionSettings;
			SetReactionActorLocation(TargetActor, ServerStartLocation, CorrectionSettings, true);
		}
		return;
	}
	if (bConsumedPendingPrediction)
	{
		AddDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);

		// Matching outcomes already played locally. A mismatch must replace the predicted montage.
		if (bPlayedLocally && PredictedReactionTag == ReactionTag)
		{
			return;
		}
	}


	if (bTargetLocallyControlled)
	{
		return;
	}

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

	ApplyLatestOlderProxyPendingFinalReactionCorrection(Context, TargetActor, TEXT("ConfirmedPreflight"));

	TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
	FPP_ReactionMovementSettings ConfirmedMovementSettings;
	ConfirmedMovementSettings.ClientInterpolationSpeed = FMath::Max(0.0f, ClientInterpolationSpeed);
	SetReactionActorLocation(TargetActor, ServerStartLocation, ConfirmedMovementSettings, true);

	PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
}


bool UPP_PredictionComponent::ConsumePendingPredictedReaction
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag* OutPredictedReactionTag, bool* bOutPlayedLocally)
{
	if (!Context.IsValid() || !TargetActor) return false;

	RemoveExpiredPendingPredictedReactions();

	for (int32 Index = PendingPredictedReactions.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PendingPredictedReaction& Entry = PendingPredictedReactions[Index];

		if (Entry.TargetActor.Get() == TargetActor && Entry.PredictionId == Context.PredictionId)
		{
			if (OutPredictedReactionTag) *OutPredictedReactionTag = Entry.ReactionTag;
			if (bOutPlayedLocally) *bOutPlayedLocally = Entry.bPlayedLocally;
			PendingPredictedReactions.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::ClientPlayOwnerConfirmedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 FGameplayTag ReactionTag,
 FVector ServerStartLocation,
 FRotator ServerStartRotation,
 float ClientInterpolationSpeed)
{
	// Owner confirmation replays visuals without letting montage root motion move the capsule.
	AActor* OwnerActor = GetOwner();
	AActor* TargetActor = OwnerActor;


	if (!OwnerActor)
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid())
	{
		return;
	}

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (!TargetCharacter) return;

	UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();
	UPP_CharacterMovementComponent* SyncMovementComponent =
		Cast<UPP_CharacterMovementComponent>(MovementComponent);
	USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
	UAnimInstance* OwnerAnimInstance = TargetMesh ? TargetMesh->GetAnimInstance() : nullptr;

	ERootMotionMode::Type SavedOwnerRootMotionMode = OwnerAnimInstance
		                                                 ? OwnerAnimInstance->RootMotionMode.GetValue()
		                                                 : ERootMotionMode::RootMotionFromMontagesOnly;

	const bool bSavedAbilityRootMotionSuppressed = SyncMovementComponent
		                                               ? SyncMovementComponent->IsAbilityRootMotionSuppressed()
		                                               : false;

	bool bAppliedCorrectionSuppression = false;
	bool bAppliedVisualOnlyRootMotion = false;
	bool bAppliedAbilityRootMotionSuppression = false;

	if (MovementComponent)
	{
		OwnerReactionCorrectionSuppressionCount++;
		bAppliedCorrectionSuppression = true;

		// Keep normal capsule/server correction and smoothing alive. Only prevent root-motion
		// correction RPCs from fast-forwarding the locally replayed montage track.
		PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, true, Context, TargetActor, ReactionTag,
		                                            TEXT("OwnerReactionStart"));
	}

	const float StartPosition = GetReactionStartPosition(Reaction);

	TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
	FPP_ReactionMovementSettings OwnerMovementSettings;
	OwnerMovementSettings.ClientInterpolationSpeed = FMath::Max(0.0f, ClientInterpolationSpeed);
	SetReactionActorLocation(TargetActor, ServerStartLocation, OwnerMovementSettings, true);


	if (SyncMovementComponent)
	{
		SyncMovementComponent->SetAbilityRootMotionSuppressed(true);
		bAppliedAbilityRootMotionSuppression = true;
	}

	if (OwnerAnimInstance)
	{
		SavedOwnerRootMotionMode = OwnerAnimInstance->RootMotionMode.GetValue();
		OwnerAnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		bAppliedVisualOnlyRootMotion = true;
	}

	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);


	if (!bPlayed)
	{
		if (bAppliedCorrectionSuppression)
		{
			OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);

			if (OwnerReactionCorrectionSuppressionCount == 0)
			{
				ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("OwnerPlayFailed"));

				if (SyncMovementComponent && bAppliedAbilityRootMotionSuppression)
				{
					SyncMovementComponent->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);
				}

				if (OwnerAnimInstance && bAppliedVisualOnlyRootMotion)
				{
					OwnerAnimInstance->SetRootMotionMode(SavedOwnerRootMotionMode);
				}

				if (MovementComponent)
				{
					PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, false, Context, TargetActor,
					                                            ReactionTag,
					                                            TEXT("OwnerPlayFailed"));
				}
			}
		}

		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);

		TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
		TWeakObjectPtr<UCharacterMovementComponent> WeakMovementComponent(MovementComponent);
		TWeakObjectPtr<UPP_CharacterMovementComponent> WeakSyncMovementComponent(SyncMovementComponent);
		TWeakObjectPtr<UAnimInstance> WeakOwnerAnimInstance(OwnerAnimInstance);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FGameplayTag CapturedReactionTag = ReactionTag;
		const FPP_ReactionPredictionContext CapturedContext = Context;
		const bool bCapturedAppliedCorrectionSuppression = bAppliedCorrectionSuppression;
		const bool bCapturedAppliedVisualOnlyRootMotion = bAppliedVisualOnlyRootMotion;
		const bool bCapturedAppliedAbilityRootMotionSuppression = bAppliedAbilityRootMotionSuppression;
		const bool bCapturedSavedAbilityRootMotionSuppressed = bSavedAbilityRootMotionSuppressed;
		const ERootMotionMode::Type CapturedSavedOwnerRootMotionMode = SavedOwnerRootMotionMode;

		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(
			TimerHandle,
			[WeakThis, WeakMovementComponent, WeakSyncMovementComponent, WeakOwnerAnimInstance, WeakTarget,
				CapturedReactionTag, CapturedContext, bCapturedAppliedCorrectionSuppression,
				bCapturedAppliedVisualOnlyRootMotion, bCapturedAppliedAbilityRootMotionSuppression,
				bCapturedSavedAbilityRootMotionSuppressed, CapturedSavedOwnerRootMotionMode]()
			{
				UPP_PredictionComponent* StrongThis = WeakThis.Get();
				AActor* StrongTargetActor = WeakTarget.Get();
				UCharacterMovementComponent* StrongMovementComponent = WeakMovementComponent.Get();
				UPP_CharacterMovementComponent* StrongSyncMovementComponent = WeakSyncMovementComponent.
					Get();
				UAnimInstance* StrongOwnerAnimInstance = WeakOwnerAnimInstance.Get();

				if (!StrongThis) return;

				if (bCapturedAppliedCorrectionSuppression)
				{
					StrongThis->OwnerReactionCorrectionSuppressionCount =
						FMath::Max(0, StrongThis->OwnerReactionCorrectionSuppressionCount - 1);
				}


				if (StrongThis->OwnerReactionCorrectionSuppressionCount > 0)
				{
					return;
				}

				if (StrongTargetActor)
				{
					StrongThis->ApplyOwnerPendingFinalReactionCorrection(CapturedContext, StrongTargetActor,
					                                                     CapturedReactionTag, TEXT("OwnerReactionEnd"));
				}

				if (StrongSyncMovementComponent && bCapturedAppliedAbilityRootMotionSuppression)
				{
					StrongSyncMovementComponent->SetAbilityRootMotionSuppressed(
						bCapturedSavedAbilityRootMotionSuppressed);
				}

				if (StrongOwnerAnimInstance && bCapturedAppliedVisualOnlyRootMotion)
				{
					StrongOwnerAnimInstance->SetRootMotionMode(CapturedSavedOwnerRootMotionMode);
				}

				if (StrongMovementComponent && bCapturedAppliedCorrectionSuppression)
				{
					PP_SetOwnerMontageTrackCorrectionSuppressed(StrongMovementComponent, false, CapturedContext,
					                                            StrongTargetActor,
					                                            CapturedReactionTag, TEXT("OwnerReactionEnd"));
				}
			},
			RemainingDuration,
			false);
	}
	else
	{
		if (bAppliedCorrectionSuppression)
		{
			OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);
		}

		if (OwnerReactionCorrectionSuppressionCount == 0)
		{
			ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("NoWorld"));

			if (SyncMovementComponent && bAppliedAbilityRootMotionSuppression)
			{
				SyncMovementComponent->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);
			}

			if (OwnerAnimInstance && bAppliedVisualOnlyRootMotion)
			{
				OwnerAnimInstance->SetRootMotionMode(SavedOwnerRootMotionMode);
			}

			if (MovementComponent)
			{
				PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, false, Context, TargetActor, ReactionTag,
				                                            TEXT("NoWorld"));
			}
		}
	}
}


void UPP_PredictionComponent::ClientFinishOwnerConfirmedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 FGameplayTag ReactionTag,
 FVector ServerFinalLocation,
 FRotator ServerFinalRotation)
{
	AActor* OwnerActor = GetOwner();
	AActor* TargetActor = OwnerActor;
	if (!OwnerActor || OwnerActor->HasAuthority() ||
		!Context.IsValid() || !ReactionTag.IsValid())
	{
		return;
	}

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (!TargetPawn || !TargetPawn->IsLocallyControlled()) return;

	AddOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
		ServerFinalLocation, ServerFinalRotation);

	if (OwnerReactionCorrectionSuppressionCount == 0)
	{
		ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
			TEXT("OwnerFinalArrivedAfterReaction"));
	}
}

void UPP_PredictionComponent::ClientFinishPredictedProxyReaction_Implementation
(FPP_ReactionPredictionContext Context,
 AActor* TargetActor,
 FGameplayTag ReactionTag,
 FVector ServerFinalLocation,
 FRotator ServerFinalRotation)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || OwnerActor->HasAuthority() || !TargetActor ||
		!Context.IsValid() || !ReactionTag.IsValid())
	{
		return;
	}

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (TargetPawn && TargetPawn->IsLocallyControlled()) return;

	const bool bHadDeferredCorrection =
		ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
	const bool bHadPendingPrediction =
		ConsumePendingPredictedReaction(Context, TargetActor);


	if (!bHadDeferredCorrection && !bHadPendingPrediction)
	{
		return;
	}

	AddProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, ServerFinalLocation, ServerFinalRotation);

	UWorld* World = GetWorld();
	if (!World || ProxyFinalCorrectionDelaySeconds <= KINDA_SMALL_NUMBER)
	{
		ApplyProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("ProxyFinalNoDelay"));
		return;
	}

	TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
	TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	const FPP_ReactionPredictionContext CapturedContext = Context;
	const FGameplayTag CapturedReactionTag = ReactionTag;

	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(
		TimerHandle,
		[WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
		{
			UPP_PredictionComponent* StrongThis = WeakThis.Get();
			AActor* StrongTarget = WeakTarget.Get();
			if (!StrongThis || !StrongTarget) return;

			StrongThis->ApplyProxyPendingFinalReactionCorrection(CapturedContext, StrongTarget,
			                                                     CapturedReactionTag, TEXT("ProxyFinalDelayElapsed"));
		},
		ProxyFinalCorrectionDelaySeconds,
		false);
}


void UPP_PredictionComponent::AddDeferredPredictedReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredDeferredPredictedReactionCorrections();

	FPP_DeferredPredictedReactionCorrection& Entry =
		DeferredPredictedReactionCorrections.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.TimeSeconds = World->GetTimeSeconds();
}

bool UPP_PredictionComponent::ConsumeDeferredPredictedReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredDeferredPredictedReactionCorrections();

	for (int32 Index = DeferredPredictedReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_DeferredPredictedReactionCorrection& Entry = DeferredPredictedReactionCorrections[Index];

		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			DeferredPredictedReactionCorrections.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::RemoveExpiredDeferredPredictedReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		DeferredPredictedReactionCorrections.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = DeferredPredictedReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_DeferredPredictedReactionCorrection& Entry = DeferredPredictedReactionCorrections[Index];

		const bool bExpired = Now - Entry.TimeSeconds > DeferredPredictedCorrectionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			DeferredPredictedReactionCorrections.RemoveAtSwap(Index);
		}
	}
}

void UPP_PredictionComponent::AddOwnerPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const FVector& ServerFinalLocation,
 const FRotator& ServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredOwnerPendingFinalReactionCorrections();

	for (FPP_OwnerPendingFinalReactionCorrection& Entry : OwnerPendingFinalReactionCorrections)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			Entry.ServerFinalLocation = ServerFinalLocation;
			Entry.ServerFinalRotation = ServerFinalRotation;
			Entry.TimeSeconds = World->GetTimeSeconds();
			return;
		}
	}

	FPP_OwnerPendingFinalReactionCorrection& Entry =
		OwnerPendingFinalReactionCorrections.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.ServerFinalLocation = ServerFinalLocation;
	Entry.ServerFinalRotation = ServerFinalRotation;
	Entry.TimeSeconds = World->GetTimeSeconds();
}

bool UPP_PredictionComponent::ConsumeOwnerPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 FVector& OutServerFinalLocation,
 FRotator& OutServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredOwnerPendingFinalReactionCorrections();

	for (int32 Index = OwnerPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = OwnerPendingFinalReactionCorrections[Index];

		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			OutServerFinalLocation = Entry.ServerFinalLocation;
			OutServerFinalRotation = Entry.ServerFinalRotation;
			OwnerPendingFinalReactionCorrections.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

bool UPP_PredictionComponent::ApplyOwnerPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const TCHAR* Reason)
{
	if (!TargetActor) return false;

	FVector ServerFinalLocation = FVector::ZeroVector;
	FRotator ServerFinalRotation = FRotator::ZeroRotator;

	if (!ConsumeOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
	                                                ServerFinalLocation, ServerFinalRotation))
	{
		return false;
	}

	StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor>(TargetActor));

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FRotator ClientFinalRotation = TargetActor->GetActorRotation();
	const float Distance = FVector::Dist(ClientFinalLocation, ServerFinalLocation);
	const float RotationDistance = FMath::Max3(
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Pitch - ClientFinalRotation.Pitch)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Yaw - ClientFinalRotation.Yaw)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Roll - ClientFinalRotation.Roll)));


	const bool bShouldApplyLocation = Distance > FinalCorrectionTolerance;
	const bool bShouldApplyRotation = RotationDistance > FinalRotationCorrectionTolerance;
	const float SmoothSeconds = FMath::Max(0.f, OwnerFinalCorrectionSmoothSeconds);


	if (!bShouldApplyLocation && !bShouldApplyRotation)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World || SmoothSeconds <= KINDA_SMALL_NUMBER)
	{
		TargetActor->SetActorLocationAndRotation(
			bShouldApplyLocation ? ServerFinalLocation : ClientFinalLocation,
			bShouldApplyRotation ? ServerFinalRotation : ClientFinalRotation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);


		return true;
	}

	TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	const FVector StartLocation = ClientFinalLocation;
	const FQuat StartQuat = ClientFinalRotation.Quaternion();
	const FVector TargetLocation = ServerFinalLocation;
	const FQuat TargetQuat = ServerFinalRotation.Quaternion();
	const float StartTime = World->GetTimeSeconds();
	const FPP_ReactionPredictionContext CapturedContext = Context;
	const FGameplayTag CapturedReactionTag = ReactionTag;
	const FString CapturedReason = Reason ? FString(Reason) : FString(TEXT("None"));

	TSharedRef<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();

	World->GetTimerManager().SetTimer(
		TimerHandle.Get(),
		[WeakTarget, StartLocation, StartQuat, TargetLocation, TargetQuat, StartTime, SmoothSeconds,
			bShouldApplyLocation, bShouldApplyRotation, CapturedContext, CapturedReactionTag, CapturedReason,
			TimerHandle]()
		{
			AActor* StrongTarget = WeakTarget.Get();
			if (!StrongTarget)
			{
				return;
			}

			UWorld* InnerWorld = StrongTarget->GetWorld();
			if (!InnerWorld)
			{
				return;
			}

			const float RawAlpha = FMath::Clamp((InnerWorld->GetTimeSeconds() - StartTime) / SmoothSeconds, 0.f, 1.f);
			const float SmoothAlpha = RawAlpha * RawAlpha * (3.f - 2.f * RawAlpha);

			const FVector CurrentLocation = bShouldApplyLocation
				                                ? FMath::Lerp(StartLocation, TargetLocation, SmoothAlpha)
				                                : StrongTarget->GetActorLocation();

			const FRotator CurrentRotation = bShouldApplyRotation
				                                 ? FQuat::Slerp(StartQuat, TargetQuat, SmoothAlpha).Rotator()
				                                 : StrongTarget->GetActorRotation();

			StrongTarget->SetActorLocationAndRotation(CurrentLocation, CurrentRotation, false, nullptr,
			                                          ETeleportType::TeleportPhysics);

			if (RawAlpha >= 1.f)
			{
				StrongTarget->SetActorLocationAndRotation(
					bShouldApplyLocation ? TargetLocation : StrongTarget->GetActorLocation(),
					bShouldApplyRotation ? TargetQuat.Rotator() : StrongTarget->GetActorRotation(),
					false,
					nullptr,
					ETeleportType::TeleportPhysics);

				InnerWorld->GetTimerManager().ClearTimer(TimerHandle.Get());
			}
		},
		0.016f,
		true);

	return true;
}

void UPP_PredictionComponent::RemoveExpiredOwnerPendingFinalReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		OwnerPendingFinalReactionCorrections.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = OwnerPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = OwnerPendingFinalReactionCorrections[Index];

		const bool bExpired = Now - Entry.TimeSeconds > DeferredPredictedCorrectionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			OwnerPendingFinalReactionCorrections.RemoveAtSwap(Index);
		}
	}
}


void UPP_PredictionComponent::AddProxyPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const FVector& ServerFinalLocation,
 const FRotator& ServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredProxyPendingFinalReactionCorrections();

	for (FPP_OwnerPendingFinalReactionCorrection& Entry : ProxyPendingFinalReactionCorrections)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			Entry.ServerFinalLocation = ServerFinalLocation;
			Entry.ServerFinalRotation = ServerFinalRotation;
			Entry.TimeSeconds = World->GetTimeSeconds();


			return;
		}
	}

	FPP_OwnerPendingFinalReactionCorrection& Entry =
		ProxyPendingFinalReactionCorrections.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.ServerFinalLocation = ServerFinalLocation;
	Entry.ServerFinalRotation = ServerFinalRotation;
	Entry.TimeSeconds = World->GetTimeSeconds();
}

bool UPP_PredictionComponent::ConsumeProxyPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 FVector& OutServerFinalLocation,
 FRotator& OutServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredProxyPendingFinalReactionCorrections();

	for (int32 Index = ProxyPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = ProxyPendingFinalReactionCorrections[Index];

		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			OutServerFinalLocation = Entry.ServerFinalLocation;
			OutServerFinalRotation = Entry.ServerFinalRotation;
			ProxyPendingFinalReactionCorrections.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

bool UPP_PredictionComponent::ApplyProxyPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const TCHAR* Reason)
{
	if (!TargetActor) return false;

	const bool bPreflightApply = Reason && FCString::Stristr(Reason, TEXT("Preflight")) != nullptr;
	if (!bPreflightApply && HasNewerReactionMarkerForTarget(Context, TargetActor))
	{
		FVector DroppedServerFinalLocation = FVector::ZeroVector;
		FRotator DroppedServerFinalRotation = FRotator::ZeroRotator;
		ConsumeProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
		                                           DroppedServerFinalLocation, DroppedServerFinalRotation);


		return false;
	}

	FVector ServerFinalLocation = FVector::ZeroVector;
	FRotator ServerFinalRotation = FRotator::ZeroRotator;

	if (!ConsumeProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
	                                                ServerFinalLocation, ServerFinalRotation))
	{
		return false;
	}

	StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor>(TargetActor));

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FRotator ClientFinalRotation = TargetActor->GetActorRotation();
	const float Distance = FVector::Dist(ClientFinalLocation, ServerFinalLocation);
	const float RotationDistance = FMath::Max3(
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Pitch - ClientFinalRotation.Pitch)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Yaw - ClientFinalRotation.Yaw)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Roll - ClientFinalRotation.Roll)));


	if (Distance <= FinalCorrectionTolerance && RotationDistance <= FinalRotationCorrectionTolerance)
	{
		return true;
	}

	if (!bApplyInstantFinalCorrection)
	{
		return false;
	}

	if (MaxInstantFinalCorrectionDistance > 0.0f && Distance > MaxInstantFinalCorrectionDistance)
	{
		// Large errors are left to normal Character Movement replication.
		return false;
	}

	if (Distance > FinalCorrectionTolerance)
	{
		TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (RotationDistance > FinalRotationCorrectionTolerance)
	{
		TargetActor->SetActorRotation(ServerFinalRotation, ETeleportType::TeleportPhysics);
	}

	return true;
}

bool UPP_PredictionComponent::ApplyLatestOlderProxyPendingFinalReactionCorrection
(
	const FPP_ReactionPredictionContext& Context, AActor* TargetActor, const TCHAR* Reason)
{
	if (!Context.IsValid() || !TargetActor) return false;

	RemoveExpiredProxyPendingFinalReactionCorrections();

	int32 BestIndex = INDEX_NONE;
	int32 BestPredictionId = INDEX_NONE;

	for (int32 Index = 0; Index < ProxyPendingFinalReactionCorrections.Num(); ++Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = ProxyPendingFinalReactionCorrections[Index];

		if (Entry.TargetActor.Get() != TargetActor) continue;
		if (!Entry.ReactionTag.IsValid()) continue;
		if (Entry.PredictionId == INDEX_NONE) continue;
		if (Entry.PredictionId >= Context.PredictionId) continue;

		if (BestIndex == INDEX_NONE || Entry.PredictionId > BestPredictionId)
		{
			BestIndex = Index;
			BestPredictionId = Entry.PredictionId;
		}
	}

	if (BestIndex == INDEX_NONE) return false;

	const FPP_OwnerPendingFinalReactionCorrection Entry = ProxyPendingFinalReactionCorrections[BestIndex];


	FPP_ReactionPredictionContext OlderContext;
	OlderContext.PredictionId = Entry.PredictionId;

	return ApplyProxyPendingFinalReactionCorrection(OlderContext, TargetActor, Entry.ReactionTag, Reason);
}

bool UPP_PredictionComponent::HasNewerReactionMarkerForTarget
(
	const FPP_ReactionPredictionContext& Context, AActor* TargetActor)
{
	if (!Context.IsValid() || !TargetActor) return false;

	RemoveExpiredPendingPredictedReactions();
	RemoveExpiredDeferredPredictedReactionCorrections();

	for (const FPP_PendingPredictedReaction& Entry : PendingPredictedReactions)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.PredictionId != INDEX_NONE &&
			Entry.PredictionId > Context.PredictionId)
		{
			return true;
		}
	}

	for (const FPP_DeferredPredictedReactionCorrection& Entry : DeferredPredictedReactionCorrections)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.PredictionId != INDEX_NONE &&
			Entry.PredictionId > Context.PredictionId)
		{
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::RemoveExpiredProxyPendingFinalReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		ProxyPendingFinalReactionCorrections.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = ProxyPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = ProxyPendingFinalReactionCorrections[Index];

		const bool bExpired = Now - Entry.TimeSeconds > DeferredPredictedCorrectionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			ProxyPendingFinalReactionCorrections.RemoveAtSwap(Index);
		}
	}
}

bool UPP_PredictionComponent::CanPlayPredictedReactionOnTargetProxy
(AActor* TargetActor,
 const FPP_ReactionDataEntry& Reaction) const
{
	if (!TargetActor || !Reaction.Montage) return false;

	const UWorld* World = GetWorld();
	if (!World) return false;

	if (World->GetNetMode() == NM_DedicatedServer) return false;

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return false;

	// Only the attacking client predicts a reaction on its remote target proxy.
	if (OwnerActor->HasAuthority()) return false;

	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return false;

	// The target we are animating should be a proxy on this machine.
	if (TargetActor->HasAuthority()) return false;

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (TargetPawn && TargetPawn->IsLocallyControlled()) return false;

	const double Now = World->GetTimeSeconds();

	if (const double* LastTime = LastReactionTimeByTarget.Find(TargetActor))
	{
		if (Now - *LastTime < Reaction.MinReplayInterval)
		{
			return false;
		}
	}

	return true;
}

FPP_ReactionPredictionContext UPP_PredictionComponent::MakeReactionPredictionContext()
{
	FPP_ReactionPredictionContext Context;
	Context.PredictionId = NextPredictionId;

	NextPredictionId = (NextPredictionId + 1) % MaxPredictionId;
	if (NextPredictionId == INDEX_NONE)
	{
		NextPredictionId = 0;
	}

	return Context;
}

void UPP_PredictionComponent::AddPendingPredictedReaction
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag, const bool bPlayedLocally)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredPendingPredictedReactions();

	FPP_PendingPredictedReaction& Entry = PendingPredictedReactions.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.TimeSeconds = World->GetTimeSeconds();
	Entry.bPlayedLocally = bPlayedLocally;


	if (PendingPredictedReactions.Num() > MaxPendingPredictedReactions)
	{
		const int32 NumToRemove = PendingPredictedReactions.Num() - MaxPendingPredictedReactions;
		PendingPredictedReactions.RemoveAt(0, NumToRemove, EAllowShrinking::No);
	}
}

void UPP_PredictionComponent::RemoveExpiredPendingPredictedReactions()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		PendingPredictedReactions.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = PendingPredictedReactions.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PendingPredictedReaction& Entry = PendingPredictedReactions[Index];

		const bool bExpired = Now - Entry.TimeSeconds > PendingPredictedReactionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			PendingPredictedReactions.RemoveAtSwap(Index);
		}
	}
}

float UPP_PredictionComponent::GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const
{
	if (!Reaction.Montage || Reaction.StartSection == NAME_None)
	{
		return 0.f;
	}

	const int32 SectionIndex = Reaction.Montage->GetSectionIndex(Reaction.StartSection);
	if (SectionIndex == INDEX_NONE)
	{
		return 0.f;
	}

	float SectionStartTime = 0.f;
	float SectionEndTime = 0.f;

	Reaction.Montage->GetSectionStartAndEndTime(SectionIndex, SectionStartTime, SectionEndTime);

	return SectionStartTime;
}

void UPP_PredictionComponent::PrepareOwnerReactionRootMotionState
(ACharacter* TargetCharacter,
 const FPP_ReactionPredictionContext& Context,
 FGameplayTag ReactionTag) const
{
	if (!TargetCharacter) return;

	UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();
	UPP_CharacterMovementComponent* SyncMoveComponent =
		Cast<UPP_CharacterMovementComponent>(MovementComponent);
	UPP_AbilityMotionComponent* MotionComponent =
		TargetCharacter->FindComponentByClass<UPP_AbilityMotionComponent>();
	USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;

	const bool bWasAbilityRootMotionSuppressed =
		SyncMoveComponent && SyncMoveComponent->IsAbilityRootMotionSuppressed();
	const bool bWasAbilityMovementInputSuppressed =
		SyncMoveComponent && SyncMoveComponent->IsAbilityMovementInputSuppressed();
	const float SavedRootMotionScale = TargetCharacter->GetAnimRootMotionTranslationScale();

	if (MotionComponent)
	{
		MotionComponent->ResetAbilityMotionState();
	}

	if (SyncMoveComponent)
	{
		SyncMoveComponent->SetAbilityRootMotionSuppressed(false);
		SyncMoveComponent->SetAbilityMovementInputSuppressed(false);
	}

	TargetCharacter->SetAnimRootMotionTranslationScale(1.0f);
	if (AnimInstance)
	{
		AnimInstance->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	}
}

bool UPP_PredictionComponent::PlayReactionMontageOnActor
(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction,
 float StartPosition, bool bForceRestart) const
{
	if (!TargetActor || !Reaction.Montage) return false;

	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (!TargetCharacter) return false;

	USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
	if (!Mesh) return false;

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance) return false;

	const bool bWasPlaying = AnimInstance->Montage_IsPlaying(Reaction.Montage);
	const float PreviousPosition = bWasPlaying ? AnimInstance->Montage_GetPosition(Reaction.Montage) : -1.f;


	if (!bForceRestart && bWasPlaying)
	{
		return true;
	}

	if (bForceRestart && bWasPlaying)
	{
		AnimInstance->Montage_Stop(0.f, Reaction.Montage);
	}

	const float PlayedLength = AnimInstance->Montage_Play(Reaction.Montage, Reaction.PlayRate);

	if (PlayedLength <= 0.f)
	{
		return false;
	}

	const float MontageLength = Reaction.Montage->GetPlayLength();
	const float ClampedStartPosition = FMath::Clamp(StartPosition,
	                                                0.f, FMath::Max(0.f, MontageLength - KINDA_SMALL_NUMBER));

	if (ClampedStartPosition > KINDA_SMALL_NUMBER)
	{
		AnimInstance->Montage_SetPosition(Reaction.Montage, ClampedStartPosition);
	}
	else if (Reaction.StartSection != NAME_None)
	{
		AnimInstance->Montage_JumpToSection(Reaction.StartSection, Reaction.Montage);
	}


	return true;
}

void UPP_PredictionComponent::ApplyTargetEffects
(AActor* InstigatorActor, AActor* TargetActor,
 const FPP_ReactionDataEntry& Reaction) const
{
	if (!InstigatorActor || !TargetActor || Reaction.TargetEffects.IsEmpty()) return;

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActor(InstigatorActor);
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	if (!TargetASC) return;

	UAbilitySystemComponent* SpecSourceASC = SourceASC ? SourceASC : TargetASC;
	for (const TSubclassOf<UGameplayEffect>& EffectClass : Reaction.TargetEffects)
	{
		if (!EffectClass) continue;

		FGameplayEffectContextHandle Context = SpecSourceASC->MakeEffectContext();
		Context.AddSourceObject(InstigatorActor);

		FGameplayEffectSpecHandle Spec = SpecSourceASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
		if (Spec.IsValid())
		{
			SpecSourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		}
	}
}

UAbilitySystemComponent* UPP_PredictionComponent::GetAbilitySystemComponentFromActor(AActor* Actor)
{
	if (!Actor) return nullptr;

	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (APlayerState* PlayerState = Character->GetPlayerState<APlayerState>())
		{
			if (UAbilitySystemComponent* PlayerStateASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerState))
			{
				return PlayerStateASC;
			}
		}
	}

	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
}

void UPP_PredictionComponent::ApplyReactionTransform
(AActor* InstigatorActor, AActor* TargetActor,
 const FPP_ReactionTransformSettings& TransformSettings)
{
	if (!InstigatorActor || !TargetActor) return;

	// Rotate first so movement can use the intended facing and reference axes.
	const FPP_ReactionRotationSettings& RotationSettings = TransformSettings.RotationSettings;
	if (RotationSettings.RotationDirection != EPP_ReactionRotationDirection::None)
	{
		AActor* ReferenceActor = ResolveReactionReferenceActor(InstigatorActor, TargetActor,
		                                                       RotationSettings.ReferenceActorSource);

		ApplyReactionTransformToRecipients(InstigatorActor, TargetActor, ReferenceActor,
		                                   RotationSettings.Recipient,
		                                   [this, &RotationSettings](AActor* RecipientActor, AActor* InReferenceActor)
		                                   {
			                                   ApplyReactionRotation(RecipientActor, InReferenceActor,
			                                                         RotationSettings);
		                                   });
	}

	const FPP_ReactionMovementSettings& MovementSettings = TransformSettings.MovementSettings;
	if (MovementSettings.MoveDirection != EPP_ReactionMoveDirection::None)
	{
		AActor* ReferenceActor = ResolveReactionReferenceActor(InstigatorActor, TargetActor,
		                                                       MovementSettings.ReferenceActorSource);

		ApplyReactionTransformToRecipients(InstigatorActor, TargetActor, ReferenceActor,
		                                   MovementSettings.Recipient,
		                                   [this, &MovementSettings](AActor* RecipientActor, AActor* InReferenceActor)
		                                   {
			                                   ApplyReactionMovement(RecipientActor, InReferenceActor,
			                                                         MovementSettings);
		                                   });
	}
}

void UPP_PredictionComponent::ApplyReactionTransformToRecipients
(AActor* InstigatorActor, AActor* TargetActor,
 AActor* ReferenceActor,
 EPP_ReactionTransformRecipient Recipient,
 TFunctionRef<void
	 (
		 AActor* RecipientActor,
		 AActor* ReferenceActor)> ApplyFunction)
{
	if (!ReferenceActor) return;

	auto ApplyToRecipient = [ReferenceActor, &ApplyFunction](AActor* RecipientActor)
	{
		if (!RecipientActor || RecipientActor == ReferenceActor) return;
		ApplyFunction(RecipientActor, ReferenceActor);
	};

	switch (Recipient)
	{
	case EPP_ReactionTransformRecipient::Instigator:
		ApplyToRecipient(InstigatorActor);
		break;

	case EPP_ReactionTransformRecipient::Target:
		ApplyToRecipient(TargetActor);
		break;

	case EPP_ReactionTransformRecipient::Both:
		ApplyToRecipient(InstigatorActor);
		if (TargetActor != InstigatorActor)
		{
			ApplyToRecipient(TargetActor);
		}
		break;

	default:
		break;
	}
}

void UPP_PredictionComponent::ApplyReactionMovement
(AActor* RecipientActor, AActor* ReferenceActor,
 const FPP_ReactionMovementSettings& MovementSettings)
{
	if (!RecipientActor || !ReferenceActor) return;

	if (MovementSettings.MoveDirection != EPP_ReactionMoveDirection::KeepCurrentDistance
		&& MovementSettings.MoveDistance <= 0.f)
	{
		return;
	}

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();

	FVector ReferenceForward = ReferenceActor->GetActorForwardVector();
	ReferenceForward.Z = 0.f;
	ReferenceForward = ReferenceForward.GetSafeNormal();
	if (ReferenceForward.IsNearlyZero()) return;

	FVector ReferenceRight = ReferenceActor->GetActorRightVector();
	ReferenceRight.Z = 0.f;
	ReferenceRight = ReferenceRight.GetSafeNormal();
	if (ReferenceRight.IsNearlyZero())
	{
		ReferenceRight = FVector::CrossProduct(FVector::UpVector, ReferenceForward).GetSafeNormal();
	}

	const FVector RelativeLocation = RecipientLocation - ReferenceLocation;
	const float CurrentForwardProjection = FVector::DotProduct(RelativeLocation, ReferenceForward);
	const float CurrentLateralProjection = FVector::DotProduct(RelativeLocation, ReferenceRight);

	float TargetForwardProjection = CurrentForwardProjection;
	float TargetLateralProjection = CurrentLateralProjection;

	switch (MovementSettings.MoveDirection)
	{
	case EPP_ReactionMoveDirection::KeepCurrentDistance:
		break;

	case EPP_ReactionMoveDirection::MoveCloser:
		TargetForwardProjection -= MovementSettings.MoveDistance;
		break;

	case EPP_ReactionMoveDirection::MoveAway:
		TargetForwardProjection += MovementSettings.MoveDistance;
		break;

	case EPP_ReactionMoveDirection::SnapToDistance:
		TargetForwardProjection = MovementSettings.MoveDistance;
		break;

	case EPP_ReactionMoveDirection::None:
	default:
		return;
	}

	switch (MovementSettings.LateralOffsetMode)
	{
	case EPP_ReactionLateralOffsetMode::KeepCurrent:
		break;

	case EPP_ReactionLateralOffsetMode::AddOffset:
		TargetLateralProjection += MovementSettings.LateralOffset;
		break;

	case EPP_ReactionLateralOffsetMode::SnapToOffset:
		TargetLateralProjection = MovementSettings.LateralOffset;
		break;

	default:
		break;
	}

	if (FMath::IsNearlyEqual(TargetForwardProjection, CurrentForwardProjection)
		&& FMath::IsNearlyEqual(TargetLateralProjection, CurrentLateralProjection))
	{
		return;
	}

	FVector NewLocation = ReferenceLocation
		+ (ReferenceForward * TargetForwardProjection)
		+ (ReferenceRight * TargetLateralProjection);
	NewLocation.Z = RecipientLocation.Z;

	SetReactionActorLocation(RecipientActor, NewLocation, MovementSettings);
}

void UPP_PredictionComponent::SetReactionActorLocation(
	AActor* RecipientActor,
	const FVector& TargetLocation,
	const FPP_ReactionMovementSettings& MovementSettings,
	bool bForceNoSweep)
{
	if (!RecipientActor) return;

	const TWeakObjectPtr<AActor> RecipientKey(RecipientActor);
	StopClientReactionMovementInterpolation(RecipientKey);

	UWorld* World = GetWorld();
	const APawn* RecipientPawn = Cast<APawn>(RecipientActor);
	ACharacter* RecipientCharacter = Cast<ACharacter>(RecipientActor);
	USkeletalMeshComponent* RecipientMesh = RecipientCharacter ? RecipientCharacter->GetMesh() : nullptr;
	const bool bCanInterpolateClient =
		World &&
		World->GetNetMode() != NM_DedicatedServer &&
		!RecipientActor->HasAuthority() &&
		MovementSettings.ClientInterpolationSpeed > KINDA_SMALL_NUMBER;
	const bool bInterpolateOwnedCapsule =
		bCanInterpolateClient && RecipientPawn && RecipientPawn->IsLocallyControlled();
	const bool bInterpolateProxyMesh =
		bCanInterpolateClient && RecipientPawn && !RecipientPawn->IsLocallyControlled() && RecipientMesh;
	const bool bSweep = !bForceNoSweep && MovementSettings.bSweep;
	const ETeleportType TeleportType = ToTeleportType(MovementSettings.TeleportType);

	if (!bInterpolateOwnedCapsule && !bInterpolateProxyMesh)
	{
		RecipientActor->SetActorLocation(TargetLocation, bSweep, nullptr, TeleportType);
		return;
	}

	constexpr float InterpolationInterval = 1.0f / 60.0f;
	const float InterpolationSpeed = MovementSettings.ClientInterpolationSpeed;
	const TWeakObjectPtr<AActor> WeakRecipient(RecipientActor);
	double LastUpdateTime = World->GetTimeSeconds();

	if (bInterpolateProxyMesh)
	{
		const FVector StartingMeshWorldLocation = RecipientMesh->GetComponentLocation();
		const FVector RestingRelativeLocation = RecipientMesh->GetRelativeLocation();
		UCharacterMovementComponent* CharacterMovement = RecipientCharacter->GetCharacterMovement();

		FPP_ProxyReactionVisualInterpolation VisualState;
		VisualState.Mesh = RecipientMesh;
		VisualState.MovementComponent = CharacterMovement;
		VisualState.RestingRelativeLocation = RestingRelativeLocation;
		if (CharacterMovement)
		{
			VisualState.SavedNetworkSmoothingMode = static_cast<uint8>(CharacterMovement->NetworkSmoothingMode);
			CharacterMovement->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}

		// Snap the replicated capsule to authority, then visually ease only the mesh.
		RecipientActor->SetActorLocation(TargetLocation, bSweep, nullptr, TeleportType);
		RecipientMesh->SetWorldLocation(StartingMeshWorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
		ProxyReactionVisualInterpolations.Add(RecipientKey, VisualState);

		FTimerDelegate VisualInterpolationDelegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, WeakRecipient, InterpolationSpeed, LastUpdateTime]() mutable
			{
				UWorld* CurrentWorld = GetWorld();
				FPP_ProxyReactionVisualInterpolation* State =
					ProxyReactionVisualInterpolations.Find(WeakRecipient);
				USkeletalMeshComponent* Mesh = State ? State->Mesh.Get() : nullptr;
				if (!CurrentWorld || !Mesh)
				{
					StopClientReactionMovementInterpolation(WeakRecipient);
					return;
				}

				const double CurrentTime = CurrentWorld->GetTimeSeconds();
				const float DeltaSeconds = static_cast<float>(FMath::Max(0.0, CurrentTime - LastUpdateTime));
				LastUpdateTime = CurrentTime;
				const FVector NextRelativeLocation = FMath::VInterpConstantTo(
					Mesh->GetRelativeLocation(), State->RestingRelativeLocation,
					DeltaSeconds, InterpolationSpeed);
				Mesh->SetRelativeLocation(NextRelativeLocation, false, nullptr, ETeleportType::None);

				if (NextRelativeLocation.Equals(State->RestingRelativeLocation, 0.1f))
				{
					StopClientReactionMovementInterpolation(WeakRecipient);
				}
			});

		FTimerHandle& TimerHandle = ClientReactionMovementInterpolationTimers.FindOrAdd(RecipientKey);
		World->GetTimerManager().SetTimer(
			TimerHandle, VisualInterpolationDelegate, InterpolationInterval, true, 0.0f);
		return;
	}

	FTimerDelegate InterpolationDelegate = FTimerDelegate::CreateWeakLambda(
		this,
		[this, WeakRecipient, TargetLocation, InterpolationSpeed, bSweep, TeleportType,
			LastUpdateTime]() mutable
		{
			AActor* StrongRecipient = WeakRecipient.Get();
			UWorld* CurrentWorld = GetWorld();
			if (!StrongRecipient || !CurrentWorld)
			{
				StopClientReactionMovementInterpolation(WeakRecipient);
				return;
			}

			const double CurrentTime = CurrentWorld->GetTimeSeconds();
			const float DeltaSeconds = static_cast<float>(FMath::Max(0.0, CurrentTime - LastUpdateTime));
			LastUpdateTime = CurrentTime;

			const FVector CurrentLocation = StrongRecipient->GetActorLocation();
			const FVector NextLocation = FMath::VInterpConstantTo(
				CurrentLocation, TargetLocation, DeltaSeconds, InterpolationSpeed);
			const bool bReachedTarget = NextLocation.Equals(TargetLocation, 0.1f);

			FHitResult SweepHit;
			StrongRecipient->SetActorLocation(
				bReachedTarget ? TargetLocation : NextLocation,
				bSweep,
				&SweepHit,
				bReachedTarget ? TeleportType : ETeleportType::None);

			if (bReachedTarget || SweepHit.bBlockingHit)
			{
				StopClientReactionMovementInterpolation(WeakRecipient);
			}
		});

	FTimerHandle& TimerHandle = ClientReactionMovementInterpolationTimers.FindOrAdd(RecipientKey);
	World->GetTimerManager().SetTimer(
		TimerHandle, InterpolationDelegate, InterpolationInterval, true, 0.0f);
}

void UPP_PredictionComponent::StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor> RecipientActor)
{
	if (FTimerHandle* TimerHandle = ClientReactionMovementInterpolationTimers.Find(RecipientActor))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(*TimerHandle);
		}
	}

	if (FPP_ProxyReactionVisualInterpolation* State = ProxyReactionVisualInterpolations.Find(RecipientActor))
	{
		if (USkeletalMeshComponent* Mesh = State->Mesh.Get())
		{
			Mesh->SetRelativeLocation(
				State->RestingRelativeLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}

		if (UCharacterMovementComponent* MovementComponent = State->MovementComponent.Get())
		{
			MovementComponent->NetworkSmoothingMode =
				static_cast<ENetworkSmoothingMode>(State->SavedNetworkSmoothingMode);
		}
	}

	ClientReactionMovementInterpolationTimers.Remove(RecipientActor);
	ProxyReactionVisualInterpolations.Remove(RecipientActor);
}

void UPP_PredictionComponent::StopAllClientReactionMovementInterpolations()
{
	TArray<TWeakObjectPtr<AActor>> Recipients;
	ClientReactionMovementInterpolationTimers.GetKeys(Recipients);
	for (const TWeakObjectPtr<AActor>& Recipient : Recipients)
	{
		StopClientReactionMovementInterpolation(Recipient);
	}

	ClientReactionMovementInterpolationTimers.Reset();
	ProxyReactionVisualInterpolations.Reset();
}

void UPP_PredictionComponent::ApplyReactionRotation
(AActor* RecipientActor, AActor* ReferenceActor,
 const FPP_ReactionRotationSettings& RotationSettings) const
{
	if (!RecipientActor || !ReferenceActor) return;

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();
	FRotator DesiredRotation = RecipientActor->GetActorRotation();

	switch (RotationSettings.RotationDirection)
	{
	case EPP_ReactionRotationDirection::FaceReferenceActor:
		{
			FVector ToReference = ReferenceLocation - RecipientLocation;
			ToReference.Z = 0.f;
			if (const FVector FacingDirection = ToReference.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPP_ReactionRotationDirection::FaceAwayFromReference:
		{
			FVector AwayFromReference = RecipientLocation - ReferenceLocation;
			AwayFromReference.Z = 0.f;
			if (const FVector FacingDirection = AwayFromReference.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPP_ReactionRotationDirection::FaceOppositeReferenceForward:
		{
			FVector ReferenceForward = RotationSettings.bUseReferenceFacingOverride
				                           ? RotationSettings.ReferenceFacingOverride.Vector()
				                           : ReferenceActor->GetActorForwardVector();

			ReferenceForward.Z = 0.f;
			ReferenceForward = ReferenceForward.GetSafeNormal();
			if (ReferenceForward.IsNearlyZero())
			{
				break;
			}

			const FVector OppositeDirection = -ReferenceForward;
			if (const FVector FacingDirection = OppositeDirection.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPP_ReactionRotationDirection::FaceDirection:
		DesiredRotation = RotationSettings.DirectionToFace;
		break;

	case EPP_ReactionRotationDirection::None:
	default:
		return;
	}

	RecipientActor->SetActorRotation(DesiredRotation, ToTeleportType(RotationSettings.TeleportType));
}

AActor* UPP_PredictionComponent::ResolveReactionReferenceActor
(AActor* InstigatorActor, AActor* TargetActor,
 EPP_ReactionReferenceActorSource ReferenceActorSource)
{
	switch (ReferenceActorSource)
	{
	case EPP_ReactionReferenceActorSource::Instigator:
		return InstigatorActor;

	case EPP_ReactionReferenceActorSource::Target:
		return TargetActor;

	default:
		return nullptr;
	}
}

ETeleportType UPP_PredictionComponent::ToTeleportType(EPP_ReactionTeleportType TeleportType)
{
	return TeleportType == EPP_ReactionTeleportType::ResetPhysics
		       ? ETeleportType::ResetPhysics
		       : ETeleportType::None;
}


