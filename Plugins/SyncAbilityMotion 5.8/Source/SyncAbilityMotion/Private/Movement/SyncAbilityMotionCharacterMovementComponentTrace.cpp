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

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bLocalOwnerReaction)
	{
		bHasOwnerReactionTraceLocation = false;
		return;
	}

	LastOwnerReactionTraceLocation = CharacterOwner->GetActorLocation();
	bHasOwnerReactionTraceLocation = true;
}