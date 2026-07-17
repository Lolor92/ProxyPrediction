#include "Prediction/Notifies/PP_CollisionNotifyState.h"
#include "Animation/AnimNotifyQueue.h"
#include "DrawDebugHelpers.h"
#include "Prediction/Component/PP_PredictionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

void UPP_CollisionNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (!MeshComp) return;
	
	AActor* OwnerActor = MeshComp->GetOwner();
	if (UPP_PredictionComponent* PredictionComponent =
		OwnerActor ? OwnerActor->FindComponentByClass<UPP_PredictionComponent>() : nullptr)
	{
		FPP_ReactionGameplayCueSettings GameplayCueSettings;
		GameplayCueSettings.Cues = GameplayCuesToExecute;
		PredictionComponent->ExecuteActivationGameplayCues(GameplayCueSettings);
	}

	if (!ShouldRunPredictedCollision(OwnerActor)) return;
	
	FTransform CurrentTransform;
	if (!BuildTraceTransform(MeshComp, CurrentTransform)) return;

	const FPP_NotifyRuntimeKey RuntimeKey = MakeRuntimeKey(MeshComp, EventReference);
	FPP_NotifyRuntimeWindow& Window = ActiveWindows.FindOrAdd(RuntimeKey);
	Window.ProcessedTargets.Reset();
	Window.PreviousSweepTransform = CurrentTransform;
}

void UPP_CollisionNotifyState::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	
	if (!MeshComp) return;

	const FPP_NotifyRuntimeKey RuntimeKey = MakeRuntimeKey(MeshComp, EventReference);
	FPP_NotifyRuntimeWindow* Window = ActiveWindows.Find(RuntimeKey);
	if (!Window) return;

	FTransform CurrentTransform;
	if (!BuildTraceTransform(MeshComp, CurrentTransform)) return;

	const float SocketDistance = FVector::Dist(
		Window->PreviousSweepTransform.GetLocation(), CurrentTransform.GetLocation());
	const bool bRejectedDiscontinuity = MaxSocketSweepDistance > 0.f && SocketDistance > MaxSocketSweepDistance;

	if (!bRejectedDiscontinuity)
	{
		SweepCollision(MeshComp, Window->PreviousSweepTransform, CurrentTransform, *Window);
	}

	Window->PreviousSweepTransform = CurrentTransform;
}

void UPP_CollisionNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	const FPP_NotifyRuntimeKey RuntimeKey = MakeRuntimeKey(MeshComp, EventReference);
	ActiveWindows.Remove(RuntimeKey);
}

bool UPP_CollisionNotifyState::ShouldRunPredictedCollision(const AActor* OwnerActor) const
{
	if (!OwnerActor) return false;

	if (OwnerActor->HasAuthority()) return true;

	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (OwnerPawn && OwnerPawn->IsLocallyControlled()) return true;

	return false;
}

FPP_NotifyRuntimeKey UPP_CollisionNotifyState::MakeRuntimeKey(
	USkeletalMeshComponent* MeshComp, const FAnimNotifyEventReference& EventReference)
{
	FPP_NotifyRuntimeKey Key;
	Key.MeshComp = MeshComp;
	Key.NotifyInstanceId = EventReference.GetNotifyInstanceID();
	return Key;
}

bool UPP_CollisionNotifyState::BuildTraceTransform(USkeletalMeshComponent* MeshComp, FTransform& OutTransform) const
{
	if (!MeshComp) return false;

	const bool bHasSocket = !SourceSocketName.IsNone() && MeshComp->DoesSocketExist(SourceSocketName);
	const FTransform SourceTransform = bHasSocket
		? MeshComp->GetSocketTransform(SourceSocketName, RTS_World)
		: MeshComp->GetComponentTransform();

	const FTransform RelativeTransform(RelativeRotation, RelativeLocation);

	OutTransform = RelativeTransform * SourceTransform;
	return true;
}

void UPP_CollisionNotifyState::SweepCollision(USkeletalMeshComponent* MeshComp, const FTransform& PreviousTransform,
	const FTransform& CurrentTransform, FPP_NotifyRuntimeWindow& Window)
{
	if (!MeshComp) return;

	AActor* OwnerActor = MeshComp->GetOwner();
	UWorld* World = MeshComp->GetWorld();
	if (!OwnerActor || !World) return;
	
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PMMO_PredictedCollisionSweep), false, OwnerActor);
	QueryParams.AddIgnoredActor(OwnerActor);
	
	const FVector PreviousLocation = PreviousTransform.GetLocation();
	const FVector CurrentLocation = CurrentTransform.GetLocation();
	const float SweepDistance = FVector::Dist(PreviousLocation, CurrentLocation);
	const float SafeStepDistance = FMath::Max(MaxSweepStepDistance, 1.f);
	const int32 UnclampedNumSteps = FMath::Max(1, FMath::CeilToInt(SweepDistance / SafeStepDistance));
	const int32 NumSteps = FMath::Min(UnclampedNumSteps, FMath::Max(1, MaxSweepSubsteps));
	const FCollisionShape Shape = MakeCollisionShape();
	UPP_PredictionComponent* PredictionComponent =
		OwnerActor->FindComponentByClass<UPP_PredictionComponent>();
	const bool bRecordAuthoritativeSweep = OwnerActor->HasAuthority() &&
		PredictionComponent && PredictedReactionTag.IsValid();
	FPP_ReactionTransformSettings AuthoritativeTransformSettings;
	FPP_ReactionDefenseSettings AuthoritativeDefenseSettings;
	FPP_ReactionDamageSettings AuthoritativeDamageSettings;
	FPP_ReactionGameplayCueSettings AuthoritativeGameplayCueSettings;
	if (bRecordAuthoritativeSweep)
	{
		BuildReactionSettings(
			AuthoritativeTransformSettings, AuthoritativeDefenseSettings,
			AuthoritativeDamageSettings, AuthoritativeGameplayCueSettings);
	}

	// Sub-sweeps cover fast socket movement between animation updates.
	for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
	{
		const float StartAlpha = static_cast<float>(StepIndex) / static_cast<float>(NumSteps);
		const float EndAlpha = static_cast<float>(StepIndex + 1) / static_cast<float>(NumSteps);
		const FVector StepStart = FMath::Lerp(PreviousLocation, CurrentLocation, StartAlpha);
		const FVector StepEnd = FMath::Lerp(PreviousLocation, CurrentLocation, EndAlpha);
		const FQuat StepRotation = FQuat::Slerp(
			PreviousTransform.GetRotation(), CurrentTransform.GetRotation(), EndAlpha).GetNormalized();

		TArray<FHitResult> Hits;
		const FCollisionObjectQueryParams ObjectQueryParams(TraceChannel.GetValue());
		const bool bHit = World->SweepMultiByObjectType(Hits, StepStart, StepEnd, StepRotation,
			ObjectQueryParams, Shape, QueryParams);

		if (bDrawDebug)
		{
			const float DrawTime = 0.25f;
			const FColor DebugColor = bHit ? FColor::Red : FColor::Green;
			const FQuat ShapeRotation = StepRotation;

			switch (CollisionShape)
			{
			case EPP_CollisionShape::Sphere:
				DrawDebugSphere(World, StepEnd, FMath::Max(SphereRadius, 1.f), 16, DebugColor, false, DrawTime);
				break;

			case EPP_CollisionShape::Capsule:
				DrawDebugCapsule(World, StepEnd, FMath::Max(CapsuleHalfHeight, 1.f),
					FMath::Max(CapsuleRadius, 1.f), ShapeRotation, DebugColor, false, DrawTime);
				break;

			case EPP_CollisionShape::Box:
			default:
				DrawDebugBox(World, StepEnd, BoxExtent.ComponentMax(FVector(1.f)), ShapeRotation, DebugColor,
					false, DrawTime);
				break;
			}
		}
		
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			const bool bSelfHit = HitActor == OwnerActor;
			const bool bAlreadyProcessed = HasAlreadyProcessedTarget(Window, HitActor);
			if (!HitActor || bSelfHit) continue;

			// Object responses can be misconfigured on individual components. Predicted reactions are actor-to-actor
			// combat events, so never mark or process world geometry and other non-pawn actors as targets.
			APawn* HitPawn = Cast<APawn>(HitActor);
			if (!HitPawn)
			{
				continue;
			}

			if (bAlreadyProcessed) continue;

			// Mark before prediction so overlapping sub-sweeps cannot replay the hit.
			MarkTargetProcessed(Window, HitActor);
			HandleCollisionTarget(OwnerActor, HitActor, Hit);
		}

		// Retain the server-authored geometry even when the target's current capsule has already
		// dashed away. The prediction component can replay this sub-sweep against synchronized
		// historical attacker/target transforms without accepting client-authored trace data.
		if (bRecordAuthoritativeSweep)
		{
			PredictionComponent->RecordAuthoritativeCollisionSweep(
				PredictedReactionTag,
				FTransform(StepRotation, StepStart),
				FTransform(StepRotation, StepEnd),
				Shape, TraceChannel.GetValue(),
				AuthoritativeTransformSettings, AuthoritativeDefenseSettings,
				AuthoritativeDamageSettings, AuthoritativeGameplayCueSettings);
		}
	}
}

FCollisionShape UPP_CollisionNotifyState::MakeCollisionShape() const
{
	switch (CollisionShape)
	{
	case EPP_CollisionShape::Sphere:
		return FCollisionShape::MakeSphere(FMath::Max(SphereRadius, 1.f));

	case EPP_CollisionShape::Capsule:
		return FCollisionShape::MakeCapsule(FMath::Max(CapsuleRadius, 1.f),
			FMath::Max(CapsuleHalfHeight, 1.f));

	case EPP_CollisionShape::Box:
	default:
		return FCollisionShape::MakeBox(BoxExtent.ComponentMax(FVector(1.f)));
	}
}

bool UPP_CollisionNotifyState::HasAlreadyProcessedTarget(
	const FPP_NotifyRuntimeWindow& Window, AActor* TargetActor)
{
	if (!TargetActor) return true;

	const TWeakObjectPtr<AActor> TargetKey(TargetActor);
	return Window.ProcessedTargets.Contains(TargetKey);
}

void UPP_CollisionNotifyState::MarkTargetProcessed(FPP_NotifyRuntimeWindow& Window, AActor* TargetActor)
{
	if (!TargetActor) return;

	const TWeakObjectPtr<AActor> TargetKey(TargetActor);
	Window.ProcessedTargets.Add(TargetKey);
}

void UPP_CollisionNotifyState::BuildReactionSettings(
	FPP_ReactionTransformSettings& OutTransformSettings,
	FPP_ReactionDefenseSettings& OutDefenseSettings,
	FPP_ReactionDamageSettings& OutDamageSettings,
	FPP_ReactionGameplayCueSettings& OutGameplayCueSettings) const
{
	OutTransformSettings.MovementSettings.MoveDirection = MoveDirection;
	OutTransformSettings.MovementSettings.Recipient = MovementRecipient;
	OutTransformSettings.MovementSettings.ReferenceActorSource = MovementReferenceActorSource;
	OutTransformSettings.MovementSettings.MoveDistance = MoveDistance;
	OutTransformSettings.MovementSettings.LateralOffsetMode = LateralOffsetMode;
	OutTransformSettings.MovementSettings.LateralOffset = LateralOffset;
	OutTransformSettings.MovementSettings.bSweep = bSweepMovement;
	OutTransformSettings.MovementSettings.TeleportType = MovementTeleportType;
	OutTransformSettings.MovementSettings.ClientInterpolationSpeed = ClientInterpolationSpeed;

	OutTransformSettings.RotationSettings.RotationDirection = RotationDirection;
	OutTransformSettings.RotationSettings.Recipient = RotationRecipient;
	OutTransformSettings.RotationSettings.ReferenceActorSource = RotationReferenceActorSource;
	OutTransformSettings.RotationSettings.DirectionToFace = DirectionToFace;
	OutTransformSettings.RotationSettings.TeleportType = RotationTeleportType;

	OutDefenseSettings.Block.bBlockable = bBlockable;
	OutDefenseSettings.Block.BlockAngleDegrees = BlockAngleDegrees;
	OutDefenseSettings.Block.bAllowMovementWhenBlocked = bAllowMovementWhenBlocked;
	OutDefenseSettings.Block.bAllowRotationWhenBlocked = bAllowRotationWhenBlocked;
	OutDefenseSettings.Dodge.bDodgeable = bDodgeable;
	OutDefenseSettings.SuperArmor.RequiredSuperArmor = RequiredSuperArmor;

	OutDamageSettings.DamageEffects = DamageEffects;
	OutDamageSettings.bApplyDamageWhenBlocked = bApplyDamageWhenBlocked;
	OutDamageSettings.bApplyDamageWhenParried = bApplyDamageWhenParried;
	OutDamageSettings.bApplyDamageWhenDodged = bApplyDamageWhenDodged;

	OutGameplayCueSettings.Cues = GameplayCuesToExecute;
}

void UPP_CollisionNotifyState::HandleCollisionTarget(
	AActor* AttackerActor, AActor* HitActor, const FHitResult& HitResult) const
{
	if (!AttackerActor || !HitActor || !PredictedReactionTag.IsValid()) return;

	UPP_PredictionComponent* PredictionComponent =
		AttackerActor->FindComponentByClass<UPP_PredictionComponent>();
	if (!PredictionComponent) return;

	FPP_ReactionTransformSettings TransformSettings;
	FPP_ReactionDefenseSettings DefenseSettings;
	FPP_ReactionDamageSettings DamageSettings;
	FPP_ReactionGameplayCueSettings GameplayCueSettings;
	BuildReactionSettings(TransformSettings, DefenseSettings, DamageSettings, GameplayCueSettings);

	if (AttackerActor->HasAuthority())
	{
		PredictionComponent->RecordAuthoritativeCollision(
			HitActor, PredictedReactionTag, TransformSettings, DefenseSettings, DamageSettings,
			GameplayCueSettings, HitResult);
		return;
	}

	TryPlayPredictedReaction(AttackerActor, HitActor, HitResult);
}

void UPP_CollisionNotifyState::TryPlayPredictedReaction(
	AActor* AttackerActor, AActor* HitActor, const FHitResult& HitResult) const
{
	if (!AttackerActor || !HitActor) return;
	
	UWorld* World = AttackerActor->GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	
	// Server does not do predicted target animation here.
	if (AttackerActor->HasAuthority()) return;
	
	const APawn* AttackerPawn = Cast<APawn>(AttackerActor);
	if (!AttackerPawn || !AttackerPawn->IsLocallyControlled()) return;
	
	// Only predict reaction on remote targets/proxies.
	if (HitActor->HasAuthority()) return;
	
	const APawn* HitPawn = Cast<APawn>(HitActor);
	if (HitPawn && HitPawn->IsLocallyControlled()) return;
	
	UPP_PredictionComponent* PredictionComponent = AttackerActor->FindComponentByClass<UPP_PredictionComponent>();
	if (!PredictionComponent) return;

	if (!PredictedReactionTag.IsValid()) return;

	const bool bReservedActivationHit =
		bAllowRepeatedPredictedHitInSameAbilityActivation ||
		PredictionComponent->TryReservePredictedCollisionTarget(HitActor, PredictedReactionTag);
	if (!bReservedActivationHit)
	{
		return;
	}

	FPP_ReactionTransformSettings TransformSettings;
	FPP_ReactionDefenseSettings DefenseSettings;
	FPP_ReactionDamageSettings DamageSettings;
	FPP_ReactionGameplayCueSettings GameplayCueSettings;
	BuildReactionSettings(TransformSettings, DefenseSettings, DamageSettings, GameplayCueSettings);

	const bool bPredictionStarted = PredictionComponent->PlayPredictedReactionOnTargetProxy(
		HitActor,
		PredictedReactionTag,
		TransformSettings,
		DefenseSettings,
		DamageSettings,
		GameplayCueSettings,
		HitResult);
	if (!bPredictionStarted && !bAllowRepeatedPredictedHitInSameAbilityActivation)
	{
		PredictionComponent->ReleasePredictedCollisionTarget(HitActor, PredictedReactionTag);
	}
}

