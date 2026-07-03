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
		OwnerCapsule->GetScaledCapsuleRadius(),
		OwnerCapsule->GetScaledCapsuleHalfHeight(),
		false);
	RootMotionCollisionProbeComponent->SetRelativeLocation(FVector(ClampedProbeDistance, 0.f, 0.f));

	if (RootMotionCollisionProbeComponent->GetCollisionEnabled() != ECollisionEnabled::QueryOnly)
	{
		RootMotionCollisionProbeComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	if (bSettingsChanged)
	{
		RootMotionCollisionProbeComponent->UpdateComponentToWorld();
		RootMotionCollisionProbeComponent->UpdateOverlaps();
		RebuildRootMotionCollisionOverlaps();
	}
}

void USyncAbilityMotionComponent::ClearRootMotionCollisionProbe()
{
	bRootMotionCollisionProbeEnabled = false;
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

	for (int32 Index = RootMotionCollisionCharacters.Num() - 1; Index >= 0; --Index)
	{
		ACharacter* OtherCharacter = RootMotionCollisionCharacters[Index].Get();
		if (!OtherCharacter || !RootMotionCollisionProbeComponent->IsOverlappingActor(OtherCharacter))
		{
			RootMotionCollisionCharacters.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		if (IsRootMotionCollisionCharacterInFront(OtherCharacter))
		{
			return true;
		}
	}

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
	AddRootMotionCollisionCharacter(Cast<ACharacter>(OtherActor));
}

void USyncAbilityMotionComponent::OnRootMotionCollisionProbeEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
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
}