#include "Notifies/PP_CollisionNotifyState.h"
#include "DrawDebugHelpers.h"
#include "Components/PP_PredictionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

void UPP_CollisionNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (!MeshComp) return;
	
	FPP_NotifyRuntimeWindow& Window = ActiveWindowsByMesh.FindOrAdd(MeshComp);
	Window.WindowId = FGuid::NewGuid();
	Window.ProcessedTargets.Reset();
	
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!ShouldRunPredictedCollision(OwnerActor)) return;
	
	FTransform CurrentTransform;
	if (!BuildTraceTransform(MeshComp, CurrentTransform)) return;
	
	PreviousTransforms.Add(MeshComp, CurrentTransform);
	
	UE_LOG(LogTemp, Warning, TEXT("SP Collision Begin Owner=%s Anim=%s"), *GetNameSafe(OwnerActor),
		*GetNameSafe(Animation));
}

void UPP_CollisionNotifyState::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	
	if (!MeshComp) return;

	FTransform* PreviousTransform = PreviousTransforms.Find(MeshComp);
	if (!PreviousTransform) return;
	
	FTransform CurrentTransform;
	if (!BuildTraceTransform(MeshComp, CurrentTransform)) return;
	
	SweepCollision(MeshComp, *PreviousTransform, CurrentTransform);

	*PreviousTransform = CurrentTransform;
}

void UPP_CollisionNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

bool UPP_CollisionNotifyState::ShouldRunPredictedCollision(const AActor* OwnerActor) const
{
	if (!OwnerActor) return false;

	if (OwnerActor->HasAuthority()) return true;

	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (OwnerPawn && OwnerPawn->IsLocallyControlled()) return true;

	return false;
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
	const FTransform& CurrentTransform)
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
	const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(SweepDistance / SafeStepDistance));
	const FCollisionShape Shape = MakeCollisionShape();
	
	float DebugRadius = 25.f;
	switch (CollisionShape)
	{
	case EPP_CollisionShape::Sphere:
		DebugRadius = FMath::Max(SphereRadius, 1.f);
		break;
	case EPP_CollisionShape::Capsule:
		DebugRadius = FMath::Max(CapsuleRadius, 1.f);
		break;
	case EPP_CollisionShape::Box:
	default:
		DebugRadius = BoxExtent.GetMax();
		break;
	}
	
	for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
	{
		const float StartAlpha = static_cast<float>(StepIndex) / static_cast<float>(NumSteps);
		const float EndAlpha = static_cast<float>(StepIndex + 1) / static_cast<float>(NumSteps);
		const FVector StepStart = FMath::Lerp(PreviousLocation, CurrentLocation, StartAlpha);
		const FVector StepEnd = FMath::Lerp(PreviousLocation, CurrentLocation, EndAlpha);

		TArray<FHitResult> Hits;
		const bool bHit = World->SweepMultiByChannel(Hits, StepStart, StepEnd, CurrentTransform.GetRotation(),
			TraceChannel.GetValue(), Shape, QueryParams);
		
		if (bDrawDebug)
		{
			const float DrawTime = 0.25f;
			const FColor DebugColor = bHit ? FColor::Red : FColor::Green;
			DrawDebugLine(World, StepStart, StepEnd, DebugColor, false, DrawTime, 0, 1.5f);

			switch (CollisionShape)
			{
			case EPP_CollisionShape::Sphere:
				DrawDebugSphere(World, StepStart, FMath::Max(SphereRadius, 1.f), 16, DebugColor, false, DrawTime);
				DrawDebugSphere(World, StepEnd, FMath::Max(SphereRadius, 1.f), 16, DebugColor, false, DrawTime);
				break;

			case EPP_CollisionShape::Capsule:
				DrawDebugCapsule(World, StepStart, FMath::Max(CapsuleHalfHeight, 1.f), FMath::Max(CapsuleRadius, 1.f),
					CurrentTransform.GetRotation(), DebugColor, false, DrawTime);
				DrawDebugCapsule(World, StepEnd, FMath::Max(CapsuleHalfHeight, 1.f), FMath::Max(CapsuleRadius, 1.f),
					CurrentTransform.GetRotation(), DebugColor, false, DrawTime);
				break;

			case EPP_CollisionShape::Box:
			default:
				DrawDebugBox(World, StepStart, BoxExtent.ComponentMax(FVector(1.f)), CurrentTransform.GetRotation(),
					DebugColor, false, DrawTime);
				DrawDebugBox(World, StepEnd, BoxExtent.ComponentMax(FVector(1.f)), CurrentTransform.GetRotation(), 
					DebugColor, false, DrawTime);
				break;
			}
		}
		
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || HitActor == OwnerActor) continue;
			if (HasAlreadyProcessedTarget(MeshComp, HitActor)) continue;

			MarkTargetProcessed(MeshComp, HitActor);

			UE_LOG(LogTemp, Warning, TEXT("Collision Hit Owner=%s HitActor=%s Location=%s"),
				*GetNameSafe(OwnerActor), *GetNameSafe(HitActor), *Hit.ImpactPoint.ToString());

			TryPlayPredictedReaction(OwnerActor, HitActor);
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

bool UPP_CollisionNotifyState::HasAlreadyProcessedTarget(USkeletalMeshComponent* MeshComp, AActor* TargetActor) const
{
	if (!MeshComp || !TargetActor) return true;

	const TWeakObjectPtr<USkeletalMeshComponent> MeshKey(MeshComp);
	const FPP_NotifyRuntimeWindow* Window = ActiveWindowsByMesh.Find(MeshKey);
	if (!Window) return false;

	const TWeakObjectPtr<AActor> TargetKey(TargetActor);
	return Window->ProcessedTargets.Contains(TargetKey);
}

void UPP_CollisionNotifyState::MarkTargetProcessed(USkeletalMeshComponent* MeshComp, AActor* TargetActor)
{
	if (!MeshComp || !TargetActor) return;

	const TWeakObjectPtr<USkeletalMeshComponent> MeshKey(MeshComp);
	FPP_NotifyRuntimeWindow& Window = ActiveWindowsByMesh.FindOrAdd(MeshKey);

	const TWeakObjectPtr<AActor> TargetKey(TargetActor);
	Window.ProcessedTargets.Add(TargetKey);
}

void UPP_CollisionNotifyState::TryPlayPredictedReaction(AActor* AttackerActor, AActor* HitActor) const
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

	PredictionComponent->PlayPredictedReactionOnTargetProxy(HitActor, PredictedReactionTag);
}
