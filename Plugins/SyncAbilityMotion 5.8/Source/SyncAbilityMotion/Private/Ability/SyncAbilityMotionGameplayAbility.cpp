#include "Ability/SyncAbilityMotionGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Component/SyncAbilityMotionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSyncAbilityMotionCamera, Log, All);

namespace
{
const TCHAR* SyncAbilityMotionNetModeToString(const UWorld* World)
{
	if (!World) return TEXT("NoWorld");

	switch (World->GetNetMode())
	{
	case NM_Client:
		return TEXT("Client");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Standalone:
		return TEXT("Standalone");
	default:
		return TEXT("Unknown");
	}
}

void LogSyncAbilityMotionCameraState(const TCHAR* Phase, const FString& AbilityName, const ACharacter* Character,
	const AController* Controller)
{
	if (!Character) return;

	const UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
	const USpringArmComponent* SpringArm = Character->FindComponentByClass<USpringArmComponent>();
	const FRotator ControlRotation = Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
	const FRotator ActorRotation = Character->GetActorRotation();
	const FRotator BaseAimRotation = Character->GetBaseAimRotation();
	const float ControlActorYawDelta = FRotator::NormalizeAxis(ControlRotation.Yaw - ActorRotation.Yaw);

	UE_LOG(LogSyncAbilityMotionCamera, Warning,
		TEXT("AbilityCamera Phase=%s Ability=%s Character=%s NetMode=%s Local=%d Authority=%d ")
		TEXT("ControlRot=%s ActorLoc=%s ActorRot=%s BaseAimRot=%s ControlActorYawDelta=%.2f ")
		TEXT("SpringArm=%s SpringArmWorldLoc=%s SpringArmWorldRot=%s SpringArmRelLoc=%s SpringArmRelRot=%s ")
		TEXT("SpringArmTargetArm=%.2f SpringArmSocketOffset=%s SpringArmTargetOffset=%s SpringArmUsePawnControlRot=%d ")
		TEXT("Camera=%s CameraWorldLoc=%s CameraWorldRot=%s CameraRelLoc=%s CameraRelRot=%s CameraUsePawnControlRot=%d"),
		Phase,
		*AbilityName,
		*GetNameSafe(Character),
		SyncAbilityMotionNetModeToString(Character->GetWorld()),
		Character->IsLocallyControlled(),
		Character->HasAuthority(),
		*ControlRotation.ToCompactString(),
		*Character->GetActorLocation().ToCompactString(),
		*ActorRotation.ToCompactString(),
		*BaseAimRotation.ToCompactString(),
		ControlActorYawDelta,
		*GetNameSafe(SpringArm),
		SpringArm ? *SpringArm->GetComponentLocation().ToCompactString() : TEXT("None"),
		SpringArm ? *SpringArm->GetComponentRotation().ToCompactString() : TEXT("None"),
		SpringArm ? *SpringArm->GetRelativeLocation().ToCompactString() : TEXT("None"),
		SpringArm ? *SpringArm->GetRelativeRotation().ToCompactString() : TEXT("None"),
		SpringArm ? SpringArm->TargetArmLength : 0.f,
		SpringArm ? *SpringArm->SocketOffset.ToCompactString() : TEXT("None"),
		SpringArm ? *SpringArm->TargetOffset.ToCompactString() : TEXT("None"),
		SpringArm && SpringArm->bUsePawnControlRotation,
		*GetNameSafe(Camera),
		Camera ? *Camera->GetComponentLocation().ToCompactString() : TEXT("None"),
		Camera ? *Camera->GetComponentRotation().ToCompactString() : TEXT("None"),
		Camera ? *Camera->GetRelativeLocation().ToCompactString() : TEXT("None"),
		Camera ? *Camera->GetRelativeRotation().ToCompactString() : TEXT("None"),
		Camera && Camera->bUsePawnControlRotation);
}

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
	ResetComboWindow();

	RotateAvatarToControllerYawOnActivate();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	OpenComboWindow();
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

	ResetComboWindow();

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

void USyncAbilityMotionGameplayAbility::OpenComboWindow()
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

void USyncAbilityMotionGameplayAbility::CloseComboWindow()
{
	bComboWindowOpen = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboWindowTimerHandle);
	}
}

void USyncAbilityMotionGameplayAbility::ResetComboWindow()
{
	CloseComboWindow();
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

	const FString AbilityName = GetName();
	LogSyncAbilityMotionCameraState(TEXT("BeforeRotateToControllerYaw"), AbilityName, Character, Controller);

	FRotator NewRot = Character->GetActorRotation();
	NewRot.Yaw = Controller->GetControlRotation().Yaw;
	Character->SetActorRotation(NewRot, ETeleportType::TeleportPhysics);
	RefreshLocalCameraAfterAbilityYaw(Character);

	LogSyncAbilityMotionCameraState(TEXT("AfterRotateToControllerYaw"), AbilityName, Character, Controller);

	if (UWorld* World = Character->GetWorld())
	{
		TWeakObjectPtr<ACharacter> WeakCharacter(Character);
		TWeakObjectPtr<AController> WeakController(Controller);

		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[WeakCharacter, WeakController, AbilityName]()
			{
				LogSyncAbilityMotionCameraState(TEXT("NextTickAfterRotateToControllerYaw"), AbilityName,
					WeakCharacter.Get(), WeakController.Get());
			}));

		FTimerHandle DelayedCameraLogHandle;
		World->GetTimerManager().SetTimer(
			DelayedCameraLogHandle,
			FTimerDelegate::CreateLambda(
				[WeakCharacter, WeakController, AbilityName]()
				{
					LogSyncAbilityMotionCameraState(TEXT("DelayedAfterRotateToControllerYaw"), AbilityName,
						WeakCharacter.Get(), WeakController.Get());
				}),
			0.05f,
			false);
	}
}
