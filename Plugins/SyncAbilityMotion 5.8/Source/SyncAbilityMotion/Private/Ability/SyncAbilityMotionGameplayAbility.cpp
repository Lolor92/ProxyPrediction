#include "Ability/SyncAbilityMotionGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Component/SyncAbilityMotionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"

USyncAbilityMotionGameplayAbility::USyncAbilityMotionGameplayAbility()
{
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	bServerRespectsRemoteAbilityCancellation = false;
	bRetriggerInstancedAbility = true;
}

void USyncAbilityMotionGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ActivationSequenceId = (ActivationSequenceId == MAX_uint32) ? 1u : (ActivationSequenceId + 1u);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void USyncAbilityMotionGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (!Character->IsLocallyControlled())
		{
			if (USyncAbilityMotionComponent* MotionComponent =
				Character->FindComponentByClass<USyncAbilityMotionComponent>())
			{
				MotionComponent->ResetAbilityMotionState();
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool USyncAbilityMotionGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return CanInterruptAnimatingAbility(ActorInfo);
}

bool USyncAbilityMotionGameplayAbility::CanInterruptAnimatingAbility(
	const FGameplayAbilityActorInfo* ActorInfo) const
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
	const USyncAbilityMotionGameplayAbility* AnimatingMotionAbility =
		Cast<USyncAbilityMotionGameplayAbility>(AnimatingAbility);
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

	const USyncAbilityMotionComponent* MotionComponent =
		Character ? Character->FindComponentByClass<USyncAbilityMotionComponent>() : nullptr;
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
	const float UnlockPercent = FMath::Clamp(
		AnimatingMotionAbility->MontageLockout.MontageProgressBeforeInterrupt,
		0.f,
		100.f);

	return PlayedPercent >= UnlockPercent;
}

bool USyncAbilityMotionGameplayAbility::ShouldPauseRootMotionForCharacterCollision(const ACharacter* Character) const
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

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SyncAbilityMotionCharacterCollisionProbe), false, Character);
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

void USyncAbilityMotionGameplayAbility::RotateAvatarToControllerYawOnActivate() const
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
	Character->SetActorRotation(NewRot, ETeleportType::ResetPhysics);
}
