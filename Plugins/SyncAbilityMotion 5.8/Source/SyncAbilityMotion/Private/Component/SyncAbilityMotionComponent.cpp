#include "Component/SyncAbilityMotionComponent.h"

#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

USyncAbilityMotionComponent::USyncAbilityMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USyncAbilityMotionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(USyncAbilityMotionComponent, AbilityMotionState, COND_SkipOwner);
}

void USyncAbilityMotionComponent::SetAbilityMotionState(const FSyncAbilityMotionState& NewState)
{
	if (AbilityMotionState == NewState) return;

	AbilityMotionState = NewState;
	ApplyAbilityMotionState(NewState);

	ACharacter* Character = GetOwnerCharacter();
	if (Character && !Character->HasAuthority())
	{
		ServerSetAbilityMotionState(NewState);
	}
}

void USyncAbilityMotionComponent::ResetAbilityMotionState()
{
	ClearRootMotionCollisionProbe();

	if (ACharacter* Character = GetOwnerCharacter())
	{
		if (USyncAbilityMotionCharacterMovementComponent* MoveComp =
			Cast<USyncAbilityMotionCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			MoveComp->SetAbilityRootMotionSuppressed(false);
			MoveComp->SetAbilityMovementInputSuppressed(false);
		}
	}

	FSyncAbilityMotionState DefaultState;
	DefaultState.bCanBlendMontage = false;
	DefaultState.bShouldBlendLowerBody = false;
	DefaultState.bRootMotionEnabled = true;
	DefaultState.bMovementInputSuppressed = false;

	SetAbilityMotionState(DefaultState);
}

void USyncAbilityMotionComponent::ConfigureRootMotionCollisionProbe(
	bool bEnabled,
	float ProbeDistance,
	float ForwardAngleDegrees)
{
	const bool bShouldEnableProbe = bEnabled && ProbeDistance > KINDA_SMALL_NUMBER;
	if (!bShouldEnableProbe)
	{
		ClearRootMotionCollisionProbe();
		return;
	}

	EnsureRootMotionCollisionProbe();
	if (!RootMotionCollisionProbeComponent) return;

	ACharacter* Character = GetOwnerCharacter();
	const UCapsuleComponent* OwnerCapsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!OwnerCapsule)
	{
		ClearRootMotionCollisionProbe();
		return;
	}

	const float ClampedProbeDistance = FMath::Max(0.f, ProbeDistance);
	const float ClampedForwardAngle = FMath::Clamp(ForwardAngleDegrees, 0.f, 180.f);
	const bool bSettingsChanged =
		!bRootMotionCollisionProbeEnabled ||
		!FMath::IsNearlyEqual(RootMotionCollisionProbeDistance, ClampedProbeDistance) ||
		!FMath::IsNearlyEqual(RootMotionCollisionForwardAngleDegrees, ClampedForwardAngle);

	bRootMotionCollisionProbeEnabled = true;
	RootMotionCollisionProbeDistance = ClampedProbeDistance;
	RootMotionCollisionForwardAngleDegrees = ClampedForwardAngle;

	RootMotionCollisionProbeComponent->SetCapsuleSize(
		OwnerCapsule->GetUnscaledCapsuleRadius(),
		OwnerCapsule->GetUnscaledCapsuleHalfHeight(),
		false);
	RootMotionCollisionProbeComponent->SetRelativeLocation(FVector(ClampedProbeDistance, 0.f, 0.f));

	if (RootMotionCollisionProbeComponent->GetCollisionEnabled() != ECollisionEnabled::QueryOnly)
	{
		RootMotionCollisionProbeComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	if (bSettingsChanged)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SAM_COLLISION PROBE_CONFIG Owner=%s Dist=%.1f Angle=%.1f Radius=%.1f HalfHeight=%.1f"),
			*GetNameSafe(Character),
			ClampedProbeDistance,
			ClampedForwardAngle,
			OwnerCapsule->GetUnscaledCapsuleRadius(),
			OwnerCapsule->GetUnscaledCapsuleHalfHeight());

		RootMotionCollisionProbeComponent->UpdateComponentToWorld();
		RootMotionCollisionProbeComponent->UpdateOverlaps();
		RebuildRootMotionCollisionOverlaps();
	}
}

void USyncAbilityMotionComponent::ClearRootMotionCollisionProbe()
{
	if (bRootMotionCollisionProbeEnabled)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SAM_COLLISION PROBE_CLEAR Owner=%s CachedOverlaps=%d LastBlocked=%d"),
			*GetNameSafe(GetOwner()),
			RootMotionCollisionCharacters.Num(),
			bLastLoggedRootMotionCollisionBlocked ? 1 : 0);
	}

	bRootMotionCollisionProbeEnabled = false;
	bLastLoggedRootMotionCollisionBlocked = false;
	RootMotionCollisionCharacters.Reset();

	if (RootMotionCollisionProbeComponent)
	{
		RootMotionCollisionProbeComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

bool USyncAbilityMotionComponent::HasRootMotionBlockingCharacterCollision()
{
	if (!bRootMotionCollisionProbeEnabled || !RootMotionCollisionProbeComponent)
	{
		return false;
	}

	ACharacter* BestRejectedCharacter = nullptr;
	float BestRejectedAngle = 0.f;
	float BestRejectedDot = -1.f;
	float RequiredDot = FMath::Cos(FMath::DegreesToRadians(RootMotionCollisionForwardAngleDegrees));

	for (int32 Index = RootMotionCollisionCharacters.Num() - 1; Index >= 0; --Index)
	{
		ACharacter* OtherCharacter = RootMotionCollisionCharacters[Index].Get();
		if (!OtherCharacter || !RootMotionCollisionProbeComponent->IsOverlappingActor(OtherCharacter))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SAM_COLLISION OVERLAP_STALE Owner=%s Other=%s"),
				*GetNameSafe(GetOwner()),
				*GetNameSafe(OtherCharacter));

			RootMotionCollisionCharacters.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		const ACharacter* Character = GetOwnerCharacter();
		if (!Character || OtherCharacter == Character)
		{
			continue;
		}

		FVector Forward = Character->GetActorForwardVector();
		Forward.Z = 0.f;
		if (!Forward.Normalize())
		{
			continue;
		}

		FVector ToOther = OtherCharacter->GetActorLocation() - Character->GetActorLocation();
		ToOther.Z = 0.f;
		if (!ToOther.Normalize())
		{
			continue;
		}

		const float Dot = FVector::DotProduct(Forward, ToOther);
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
		if (Dot >= RequiredDot)
		{
			if (!bLastLoggedRootMotionCollisionBlocked)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("SAM_COLLISION BLOCKED Owner=%s Other=%s Angle=%.1f MaxAngle=%.1f Dot=%.3f RequiredDot=%.3f CachedOverlaps=%d"),
					*GetNameSafe(Character),
					*GetNameSafe(OtherCharacter),
					Angle,
					RootMotionCollisionForwardAngleDegrees,
					Dot,
					RequiredDot,
					RootMotionCollisionCharacters.Num());
			}

			bLastLoggedRootMotionCollisionBlocked = true;
			return true;
		}

		if (!BestRejectedCharacter || Dot > BestRejectedDot)
		{
			BestRejectedCharacter = OtherCharacter;
			BestRejectedAngle = Angle;
			BestRejectedDot = Dot;
		}
	}

	if (bLastLoggedRootMotionCollisionBlocked || BestRejectedCharacter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SAM_COLLISION UNBLOCKED Owner=%s Reason=%s BestOther=%s BestAngle=%.1f MaxAngle=%.1f BestDot=%.3f RequiredDot=%.3f CachedOverlaps=%d"),
			*GetNameSafe(GetOwner()),
			BestRejectedCharacter ? TEXT("ANGLE_REJECT") : TEXT("NO_OVERLAP"),
			*GetNameSafe(BestRejectedCharacter),
			BestRejectedAngle,
			RootMotionCollisionForwardAngleDegrees,
			BestRejectedDot,
			RequiredDot,
			RootMotionCollisionCharacters.Num());
	}

	bLastLoggedRootMotionCollisionBlocked = false;
	return false;
}

void USyncAbilityMotionComponent::ServerSetAbilityMotionState_Implementation(const FSyncAbilityMotionState& NewState)
{
	if (AbilityMotionState == NewState) return;

	AbilityMotionState = NewState;
	ApplyAbilityMotionState(NewState);
}

void USyncAbilityMotionComponent::OnRep_AbilityMotionState()
{
	const ACharacter* Character = GetOwnerCharacter();
	if (Character && Character->IsLocallyControlled())
	{
		return;
	}

	ApplyAbilityMotionState(AbilityMotionState);
}

void USyncAbilityMotionComponent::OnRootMotionCollisionProbeBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	UE_LOG(LogTemp, Warning,
		TEXT("SAM_COLLISION OVERLAP_BEGIN Owner=%s Other=%s OtherComp=%s FromSweep=%d"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherActor),
		*GetNameSafe(OtherComp),
		bFromSweep ? 1 : 0);

	AddRootMotionCollisionCharacter(Cast<ACharacter>(OtherActor));
}

void USyncAbilityMotionComponent::OnRootMotionCollisionProbeEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	UE_LOG(LogTemp, Warning,
		TEXT("SAM_COLLISION OVERLAP_END Owner=%s Other=%s OtherComp=%s StillOverlapping=%d"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherActor),
		*GetNameSafe(OtherComp),
		RootMotionCollisionProbeComponent && RootMotionCollisionProbeComponent->IsOverlappingActor(OtherActor) ? 1 : 0);

	ACharacter* OtherCharacter = Cast<ACharacter>(OtherActor);
	if (!OtherCharacter || !RootMotionCollisionProbeComponent ||
		RootMotionCollisionProbeComponent->IsOverlappingActor(OtherCharacter))
	{
		return;
	}

	RemoveRootMotionCollisionCharacter(OtherCharacter);
}

void USyncAbilityMotionComponent::ApplyAbilityMotionState(const FSyncAbilityMotionState& NewState)
{
	ACharacter* Character = GetOwnerCharacter();
	if (!Character) return;

	USkeletalMeshComponent* MeshComp = Character->GetMesh();
	if (!MeshComp) return;

	USyncAbilityMotionAnimInstance* AnimInstance =
		Cast<USyncAbilityMotionAnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	AnimInstance->bCanBlendMontage = NewState.bCanBlendMontage;
	AnimInstance->bShouldBlendLowerBody = NewState.bShouldBlendLowerBody;

	USyncAbilityMotionCharacterMovementComponent* MoveComp =
		Cast<USyncAbilityMotionCharacterMovementComponent>(Character->GetCharacterMovement());
	const bool bUseMovementComponent =
		MoveComp && (Character->IsLocallyControlled() || (Character->HasAuthority() && !Character->IsPlayerControlled()));

	if (bUseMovementComponent)
	{
		MoveComp->SetAbilityRootMotionSuppressed(!NewState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(NewState.bMovementInputSuppressed);
		MoveComp->RefreshAbilityRootMotionMode();
		return;
	}

	if (Character->HasAuthority())
	{
		return;
	}

	AnimInstance->bRootMotionEnabled = NewState.bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(NewState.bRootMotionEnabled
		? ERootMotionMode::RootMotionFromMontagesOnly
		: ERootMotionMode::IgnoreRootMotion);
}

ACharacter* USyncAbilityMotionComponent::GetOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

void USyncAbilityMotionComponent::EnsureRootMotionCollisionProbe()
{
	if (RootMotionCollisionProbeComponent) return;

	ACharacter* Character = GetOwnerCharacter();
	UCapsuleComponent* OwnerCapsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Character || !OwnerCapsule) return;

	RootMotionCollisionProbeComponent = NewObject<UCapsuleComponent>(Character, TEXT("SyncAbilityMotionRootMotionCollisionProbe"));
	if (!RootMotionCollisionProbeComponent) return;

	RootMotionCollisionProbeComponent->SetHiddenInGame(true);
	RootMotionCollisionProbeComponent->SetGenerateOverlapEvents(true);
	RootMotionCollisionProbeComponent->SetCollisionObjectType(ECC_WorldDynamic);
	RootMotionCollisionProbeComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootMotionCollisionProbeComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	RootMotionCollisionProbeComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RootMotionCollisionProbeComponent->SetCanEverAffectNavigation(false);
	RootMotionCollisionProbeComponent->SetupAttachment(OwnerCapsule);
	RootMotionCollisionProbeComponent->RegisterComponent();

	RootMotionCollisionProbeComponent->OnComponentBeginOverlap.AddUniqueDynamic(
		this,
		&ThisClass::OnRootMotionCollisionProbeBeginOverlap);
	RootMotionCollisionProbeComponent->OnComponentEndOverlap.AddUniqueDynamic(
		this,
		&ThisClass::OnRootMotionCollisionProbeEndOverlap);
}

void USyncAbilityMotionComponent::RebuildRootMotionCollisionOverlaps()
{
	RootMotionCollisionCharacters.Reset();

	if (!RootMotionCollisionProbeComponent) return;

	TArray<AActor*> OverlappingActors;
	RootMotionCollisionProbeComponent->GetOverlappingActors(OverlappingActors, ACharacter::StaticClass());

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_COLLISION OVERLAP_REBUILD Owner=%s Count=%d"),
		*GetNameSafe(GetOwner()),
		OverlappingActors.Num());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		AddRootMotionCollisionCharacter(Cast<ACharacter>(OverlappingActor));
	}
}

bool USyncAbilityMotionComponent::IsRootMotionCollisionCharacterInFront(const ACharacter* OtherCharacter) const
{
	const ACharacter* Character = GetOwnerCharacter();
	if (!Character || !OtherCharacter || OtherCharacter == Character) return false;

	FVector Forward = Character->GetActorForwardVector();
	Forward.Z = 0.f;
	if (!Forward.Normalize()) return false;

	FVector ToOther = OtherCharacter->GetActorLocation() - Character->GetActorLocation();
	ToOther.Z = 0.f;
	if (!ToOther.Normalize()) return false;

	const float MinForwardDot = FMath::Cos(FMath::DegreesToRadians(RootMotionCollisionForwardAngleDegrees));
	return FVector::DotProduct(Forward, ToOther) >= MinForwardDot;
}

void USyncAbilityMotionComponent::AddRootMotionCollisionCharacter(ACharacter* OtherCharacter)
{
	const ACharacter* Character = GetOwnerCharacter();
	if (!OtherCharacter || OtherCharacter == Character) return;

	for (const TWeakObjectPtr<ACharacter>& ExistingCharacter : RootMotionCollisionCharacters)
	{
		if (ExistingCharacter.Get() == OtherCharacter)
		{
			return;
		}
	}

	RootMotionCollisionCharacters.Add(OtherCharacter);

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_COLLISION CACHE_ADD Owner=%s Other=%s CachedOverlaps=%d"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherCharacter),
		RootMotionCollisionCharacters.Num());
}

void USyncAbilityMotionComponent::RemoveRootMotionCollisionCharacter(ACharacter* OtherCharacter)
{
	if (!OtherCharacter) return;

	for (int32 Index = RootMotionCollisionCharacters.Num() - 1; Index >= 0; --Index)
	{
		if (!RootMotionCollisionCharacters[Index].IsValid() || RootMotionCollisionCharacters[Index].Get() == OtherCharacter)
		{
			RootMotionCollisionCharacters.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_COLLISION CACHE_REMOVE Owner=%s Other=%s CachedOverlaps=%d"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherCharacter),
		RootMotionCollisionCharacters.Num());
}