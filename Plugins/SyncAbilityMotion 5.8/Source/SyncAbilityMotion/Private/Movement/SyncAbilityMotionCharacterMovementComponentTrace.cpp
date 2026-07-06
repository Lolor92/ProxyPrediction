#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"

#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

void USyncAbilityMotionCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	const bool bLocalOwnerReaction = bIgnoreServerRootMotionMontageTrackCorrection && CharacterOwner &&
		CharacterOwner->IsLocallyControlled();

	if (bLocalOwnerReaction)
	{
		USkeletalMeshComponent* MeshComp = CharacterOwner->GetMesh();
		UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
		if (AnimInstance)
		{
			if (USyncAbilityMotionAnimInstance* SyncAnimInstance = Cast<USyncAbilityMotionAnimInstance>(AnimInstance))
			{
				SyncAnimInstance->bRootMotionEnabled = true;
			}

			AnimInstance->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		}
	}

	const FVector LocBeforeTick = CharacterOwner ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
	const FVector VelBeforeTick = Velocity;

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FVector LocAfterTick = CharacterOwner ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
	const float TickDelta2D = FVector::Dist2D(LocBeforeTick, LocAfterTick);

	if (CharacterOwner && CharacterOwner->IsLocallyControlled() && (TickDelta2D > 50.f || Velocity.Size2D() > 800.f || bLocalOwnerReaction))
	{
		UAnimInstance* TickAnimInstance = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;

		UE_LOG(LogTemp, Warning,
			TEXT("SAM_MOVE_TICK Owner=%s DT=%.4f Local=%d Auth=%d LocalOwnerReaction=%d IgnoreTrackCorrection=%d LocBefore=%s LocAfter=%s Delta2D=%.2f VelBefore=%s VelAfter=%s MoveMode=%d RootMotionMode=%d"),
			*GetNameSafe(CharacterOwner),
			DeltaTime,
			CharacterOwner->IsLocallyControlled(),
			CharacterOwner->HasAuthority(),
			bLocalOwnerReaction,
			bIgnoreServerRootMotionMontageTrackCorrection,
			*LocBeforeTick.ToCompactString(),
			*LocAfterTick.ToCompactString(),
			TickDelta2D,
			*VelBeforeTick.ToCompactString(),
			*Velocity.ToCompactString(),
			MovementMode,
			TickAnimInstance ? TickAnimInstance->RootMotionMode.GetValue() : -1);
	}

	if (!bLocalOwnerReaction)
	{
		bHasOwnerReactionTraceLocation = false;
		return;
	}

	LastOwnerReactionTraceLocation = CharacterOwner->GetActorLocation();
	bHasOwnerReactionTraceLocation = true;
}