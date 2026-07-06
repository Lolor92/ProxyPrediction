#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

void USyncAbilityMotionCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIgnoreServerRootMotionMontageTrackCorrection || !CharacterOwner || !CharacterOwner->IsLocallyControlled())
	{
		bHasOwnerReactionTraceLocation = false;
		return;
	}

	USkeletalMeshComponent* MeshComp = CharacterOwner->GetMesh();
	UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	const FAnimMontageInstance* MontageInstance = CharacterOwner->GetRootMotionAnimMontageInstance();
	const FVector CurrentLoc = CharacterOwner->GetActorLocation();
	const FVector DeltaLoc = bHasOwnerReactionTraceLocation ? CurrentLoc - LastOwnerReactionTraceLocation : FVector::ZeroVector;
	const float DeltaDist = bHasOwnerReactionTraceLocation ? DeltaLoc.Size() : 0.f;

	UE_LOG(LogTemp, Warning,
		TEXT("PP_REACTION_OWNER_MOVE_TRACE Time=%.3f Character=%s Dt=%.4f Loc=%s Delta=%s DeltaDist=%.2f Vel=%s Accel=%s PendingInput=%s MoveMode=%d HasAnimRM=%d HasRMSources=%d AbilityRMSuppressed=%d InputSuppressed=%d RootMotionMode=%d Montage=%s MontageTrack=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
		*GetNameSafe(CharacterOwner),
		DeltaTime,
		*CurrentLoc.ToCompactString(),
		*DeltaLoc.ToCompactString(),
		DeltaDist,
		*Velocity.ToCompactString(),
		*GetCurrentAcceleration().ToCompactString(),
		*CharacterOwner->GetPendingMovementInputVector().ToCompactString(),
		static_cast<int32>(MovementMode),
		HasAnimRootMotion() ? 1 : 0,
		CurrentRootMotion.HasActiveRootMotionSources() ? 1 : 0,
		bAbilityRootMotionSuppressed ? 1 : 0,
		bAbilityMovementInputSuppressed ? 1 : 0,
		AnimInstance ? static_cast<int32>(AnimInstance->RootMotionMode.GetValue()) : -1,
		MontageInstance ? *GetNameSafe(MontageInstance->Montage) : TEXT("None"),
		MontageInstance ? MontageInstance->GetPosition() : -1.f);

	LastOwnerReactionTraceLocation = CurrentLoc;
	bHasOwnerReactionTraceLocation = true;
}