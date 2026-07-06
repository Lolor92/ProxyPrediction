#include "Component/SyncAbilityMotionComponent.h"

#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

namespace SyncAbilityMotionCollisionProbe
{
	constexpr float LostOverlapGraceSeconds = 0.12f;
	constexpr float AngleReleaseGraceDegrees = 5.f;
	constexpr float ForwardContactPadding = 8.f;
	constexpr float ContactCenterAngleToleranceDegrees = 15.f;

	bool IsForwardContactBlocking(
		const ACharacter* Character,
		const ACharacter* OtherCharacter,
		float ProbeDistance,
		float RequiredDot,
		float GraceRequiredDot,
		bool bAllowGraceAngle,
		float& OutAngle,
		float& OutDot)
	{
		OutAngle = 0.f;
		OutDot = -1.f;

		const UCapsuleComponent* OwnerCapsule = Character ? Character->GetCapsuleComponent() : nullptr;
		const UCapsuleComponent* OtherCapsule = OtherCharacter ? OtherCharacter->GetCapsuleComponent() : nullptr;
		if (!Character || !OtherCharacter || OtherCharacter == Character || !OwnerCapsule || !OtherCapsule)
		{
			return false;
		}

		FVector Forward = Character->GetActorForwardVector();
		Forward.Z = 0.f;
		if (!Forward.Normalize())
		{
			return false;
		}

		FVector ToOther = OtherCharacter->GetActorLocation() - Character->GetActorLocation();
		ToOther.Z = 0.f;
		if (ToOther.IsNearlyZero())
		{
			return false;
		}

		const FVector CenterDirection = ToOther.GetSafeNormal2D();
		const float CenterDot = FVector::DotProduct(Forward, CenterDirection);
		const float RequiredAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(RequiredDot, -1.f, 1.f)));
		const float GraceAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GraceRequiredDot, -1.f, 1.f)));
		const float MaxCenterContactAngleDegrees = FMath::Clamp(
			(bAllowGraceAngle ? GraceAngleDegrees : RequiredAngleDegrees) +
			SyncAbilityMotionCollisionProbe::ContactCenterAngleToleranceDegrees,
			0.f,
			180.f);
		const float MinCenterContactDot = FMath::Cos(FMath::DegreesToRadians(MaxCenterContactAngleDegrees));
		if (CenterDot < MinCenterContactDot)
		{
			return false;
		}

		const float ForwardDistance = FVector::DotProduct(ToOther, Forward);
		const float CombinedRadius =
			OwnerCapsule->GetUnscaledCapsuleRadius() +
			OtherCapsule->GetUnscaledCapsuleRadius() +
			ForwardContactPadding;

		if (ForwardDistance <= 0.f || ForwardDistance > ProbeDistance + CombinedRadius)
		{
			return false;
		}

		const FVector Lateral = ToOther - Forward * ForwardDistance;
		const float LateralDistanceSquared = Lateral.SizeSquared2D();
		if (LateralDistanceSquared > FMath::Square(CombinedRadius))
		{
			return false;
		}

		const float LateralDistance = FMath::Sqrt(LateralDistanceSquared);
		const float ContactLateralDistance = FMath::Max(0.f, LateralDistance - CombinedRadius);
		FVector ContactDirection = Forward * ForwardDistance;
		if (ContactLateralDistance > KINDA_SMALL_NUMBER && LateralDistance > KINDA_SMALL_NUMBER)
		{
			ContactDirection += Lateral.GetSafeNormal2D() * ContactLateralDistance;
		}

		ContactDirection.Z = 0.f;
		if (!ContactDirection.Normalize())
		{
			return false;
		}

		OutDot = FVector::DotProduct(Forward, ContactDirection);
		OutAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(OutDot, -1.f, 1.f)));
		return OutDot >= RequiredDot || (bAllowGraceAngle && OutDot >= GraceRequiredDot);
	}
}

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

void USyncAbilityMotionComponent::SetServerMovementCorrectionIgnoreForAbility(bool bEnabled)
{
	ACharacter* Character = GetOwnerCharacter();
	if (!Character)
	{
		return;
	}

	if (Character->HasAuthority())
	{
		ApplyServerMovementCorrectionIgnoreForAbility(bEnabled);
		return;
	}

	if (bLastRequestedServerMovementCorrectionIgnore == bEnabled)
	{
		return;
	}

	bLastRequestedServerMovementCorrectionIgnore = bEnabled;
	ServerSetServerMovementCorrectionIgnoreForAbility(bEnabled);
}

void USyncAbilityMotionComponent::ServerSetServerMovementCorrectionIgnoreForAbility_Implementation(bool bEnabled)
{
	ApplyServerMovementCorrectionIgnoreForAbility(bEnabled);
}

void USyncAbilityMotionComponent::ApplyServerMovementCorrectionIgnoreForAbility(bool bEnabled)
{
	ACharacter* Character = GetOwnerCharacter();
	UCharacterMovementComponent* MoveComp = Character ? Character->GetCharacterMovement() : nullptr;
	if (!MoveComp)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_SERVER_CORRECTION_IGNORE Owner=%s Auth=%d Local=%d Enabled=%d CurrentIgnoreErrorChecks=%d Loc=%s Vel=%s"),
		*GetNameSafe(Character),
		Character ? Character->HasAuthority() : false,
		Character ? Character->IsLocallyControlled() : false,
		bEnabled,
		MoveComp->bIgnoreClientMovementErrorChecksAndCorrection,
		Character ? *Character->GetActorLocation().ToCompactString() : TEXT("None"),
		*MoveComp->Velocity.ToCompactString());

	if (bEnabled)
	{
		if (!bHasSavedServerMovementCorrectionIgnore)
		{
			bSavedServerIgnoreClientMovementErrorChecksAndCorrection = MoveComp->bIgnoreClientMovementErrorChecksAndCorrection;
			bHasSavedServerMovementCorrectionIgnore = true;

					}

		MoveComp->bIgnoreClientMovementErrorChecksAndCorrection = true;

		UE_LOG(LogTemp, Warning,
			TEXT("SAM_SERVER_CORRECTION_IGNORE_APPLIED Owner=%s Enabled=1 NewIgnoreErrorChecks=%d"),
			*GetNameSafe(Character),
			MoveComp->bIgnoreClientMovementErrorChecksAndCorrection);

		return;
	}

	if (!bHasSavedServerMovementCorrectionIgnore)
	{
		return;
	}

	MoveComp->bIgnoreClientMovementErrorChecksAndCorrection = bSavedServerIgnoreClientMovementErrorChecksAndCorrection;

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_SERVER_CORRECTION_IGNORE_RESTORED Owner=%s Enabled=0 RestoredIgnoreErrorChecks=%d"),
		*GetNameSafe(Character),
		MoveComp->bIgnoreClientMovementErrorChecksAndCorrection);

	
	bHasSavedServerMovementCorrectionIgnore = false;
}

void USyncAbilityMotionComponent::ConfigureRootMotionCollisionProbe(
	bool bEnabled,
	float ProbeDistance,
	float ForwardAngleDegrees,
	float FallbackProbeDistance)
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
	const float ClampedFallbackProbeDistance = FMath::Max(ClampedProbeDistance, FallbackProbeDistance);
	const float ClampedForwardAngle = FMath::Clamp(ForwardAngleDegrees, 0.f, 180.f);
	const bool bSettingsChanged =
		!bRootMotionCollisionProbeEnabled ||
		!FMath::IsNearlyEqual(RootMotionCollisionProbeDistance, ClampedProbeDistance) ||
		!FMath::IsNearlyEqual(RootMotionCollisionFallbackProbeDistance, ClampedFallbackProbeDistance) ||
		!FMath::IsNearlyEqual(RootMotionCollisionForwardAngleDegrees, ClampedForwardAngle);

	bRootMotionCollisionProbeEnabled = true;
	RootMotionCollisionProbeDistance = ClampedProbeDistance;
	RootMotionCollisionFallbackProbeDistance = ClampedFallbackProbeDistance;
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
		
		RootMotionCollisionProbeComponent->UpdateComponentToWorld();
		RootMotionCollisionProbeComponent->UpdateOverlaps();
		RebuildRootMotionCollisionOverlaps();
	}
}

void USyncAbilityMotionComponent::ClearRootMotionCollisionProbe()
{
	if (bRootMotionCollisionProbeEnabled)
	{
			}

	bRootMotionCollisionProbeEnabled = false;
	bLastLoggedRootMotionCollisionBlocked = false;
	LastRootMotionCollisionBlockTimeSeconds = -1000.f;
	RootMotionCollisionProbeDistance = 0.f;
	RootMotionCollisionFallbackProbeDistance = 0.f;
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

	const UWorld* World = GetWorld();
	const float NowSeconds = World ? World->GetTimeSeconds() : 0.f;

	ACharacter* BestRejectedCharacter = nullptr;
	float BestRejectedAngle = 0.f;
	float BestRejectedDot = -1.f;
	const float RequiredDot = FMath::Cos(FMath::DegreesToRadians(RootMotionCollisionForwardAngleDegrees));
	const float GraceAngleDegrees = FMath::Clamp(
		RootMotionCollisionForwardAngleDegrees + SyncAbilityMotionCollisionProbe::AngleReleaseGraceDegrees,
		0.f,
		180.f);
	const float GraceRequiredDot = FMath::Cos(FMath::DegreesToRadians(GraceAngleDegrees));

	for (int32 Index = RootMotionCollisionCharacters.Num() - 1; Index >= 0; --Index)
	{
		ACharacter* OtherCharacter = RootMotionCollisionCharacters[Index].Get();
		UCapsuleComponent* OtherCapsule = OtherCharacter ? OtherCharacter->GetCapsuleComponent() : nullptr;
		if (!OtherCharacter || !OtherCapsule || !RootMotionCollisionProbeComponent->IsOverlappingComponent(OtherCapsule))
		{
			
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
		const bool bStrictAngleBlock = Dot >= RequiredDot;
		const bool bAngleGraceBlock =
			!bStrictAngleBlock &&
			bLastLoggedRootMotionCollisionBlocked &&
			Dot >= GraceRequiredDot;

		float ContactAngle = 0.f;
		float ContactDot = -1.f;
		const bool bContactBlock = SyncAbilityMotionCollisionProbe::IsForwardContactBlocking(
			Character,
			OtherCharacter,
			RootMotionCollisionProbeDistance,
			RequiredDot,
			GraceRequiredDot,
			bLastLoggedRootMotionCollisionBlocked,
			ContactAngle,
			ContactDot);

		// Side-graze diagnostic:
		// Do not pause root motion for contact-only capsule rubs.
		// Only pause when the target center is inside the forward blocking cone.
		if (bContactBlock && !bStrictAngleBlock && !bAngleGraceBlock)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SAM_COLLISION_CONTACT_ONLY_IGNORED Owner=%s Other=%s Dot=%.3f Angle=%.2f ContactDot=%.3f ContactAngle=%.2f ProbeDistance=%.2f"),
				*GetNameSafe(Character),
				*GetNameSafe(OtherCharacter),
				Dot,
				Angle,
				ContactDot,
				ContactAngle,
				RootMotionCollisionProbeDistance);

			continue;
		}

		if (bStrictAngleBlock || bAngleGraceBlock)
		{
			if (!bLastLoggedRootMotionCollisionBlocked)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("SAM_COLLISION_FRONT_BLOCK Owner=%s Other=%s Dot=%.3f Angle=%.2f ProbeDistance=%.2f"),
					*GetNameSafe(Character),
					*GetNameSafe(OtherCharacter),
					Dot,
					Angle,
					RootMotionCollisionProbeDistance);
			}
			else if (bAngleGraceBlock)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("SAM_COLLISION_FRONT_GRACE_BLOCK Owner=%s Other=%s Dot=%.3f Angle=%.2f ProbeDistance=%.2f"),
					*GetNameSafe(Character),
					*GetNameSafe(OtherCharacter),
					Dot,
					Angle,
					RootMotionCollisionProbeDistance);
			}

			bLastLoggedRootMotionCollisionBlocked = true;
			LastRootMotionCollisionBlockTimeSeconds = NowSeconds;
			return true;
		}

		if (!BestRejectedCharacter || Dot > BestRejectedDot)
		{
			BestRejectedCharacter = OtherCharacter;
			BestRejectedAngle = Angle;
			BestRejectedDot = Dot;
		}
	}

	const float SecondsSinceBlock = NowSeconds - LastRootMotionCollisionBlockTimeSeconds;
	const bool bLostOverlapGraceBlock =
		bLastLoggedRootMotionCollisionBlocked &&
		SecondsSinceBlock >= 0.f &&
		SecondsSinceBlock <= SyncAbilityMotionCollisionProbe::LostOverlapGraceSeconds;

	if (bLostOverlapGraceBlock)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SAM_COLLISION_LOST_OVERLAP_GRACE_IGNORED_FOR_GRAZE_TEST SecondsSinceBlock=%.3f"),
			SecondsSinceBlock);

		// Diagnostic test: do not extend root-motion pause after a side-graze block.
		// If this removes the lag, we can replace it with stricter front-cone-only grace.
	}

	float FallbackAngle = 0.f;
	float FallbackDot = -1.f;
	ACharacter* FallbackCharacter = nullptr;
	if (bLastLoggedRootMotionCollisionBlocked &&
		HasFallbackRootMotionBlockingCharacterCollision(RequiredDot, GraceRequiredDot, FallbackAngle, FallbackDot, FallbackCharacter))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SAM_COLLISION_FALLBACK_IGNORED_FOR_GRAZE_TEST Owner=%s Other=%s Dot=%.3f Angle=%.2f"),
			*GetNameSafe(GetOwnerCharacter()),
			*GetNameSafe(FallbackCharacter),
			FallbackDot,
			FallbackAngle);

		// Diagnostic test: do not extend root-motion pause from fallback contact-only detection.
	}

	if (bLastLoggedRootMotionCollisionBlocked || BestRejectedCharacter)
	{
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
	ACharacter* OtherCharacter = Cast<ACharacter>(OtherActor);
	const UCapsuleComponent* OtherCapsule = OtherCharacter ? OtherCharacter->GetCapsuleComponent() : nullptr;
	const bool bIsCharacterCapsule = OtherCapsule && OtherComp == OtherCapsule;

	
	if (!bIsCharacterCapsule)
	{
		return;
	}

	AddRootMotionCollisionCharacter(OtherCharacter);
}

void USyncAbilityMotionComponent::OnRootMotionCollisionProbeEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	ACharacter* OtherCharacter = Cast<ACharacter>(OtherActor);
	UCapsuleComponent* OtherCapsule = OtherCharacter ? OtherCharacter->GetCapsuleComponent() : nullptr;
	const bool bIsCharacterCapsule = OtherCapsule && OtherComp == OtherCapsule;
	const bool bStillOverlappingCapsule =
		RootMotionCollisionProbeComponent && OtherCapsule &&
		RootMotionCollisionProbeComponent->IsOverlappingComponent(OtherCapsule);

	
	if (!bIsCharacterCapsule || bStillOverlappingCapsule)
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

	UE_LOG(LogTemp, Warning,
		TEXT("SAM_APPLY_STATE Owner=%s Local=%d Auth=%d PlayerControlled=%d UseMoveComp=%d RootMotionEnabled=%d MoveInputSuppressed=%d CanBlend=%d LowerBody=%d Loc=%s Vel=%s"),
		*GetNameSafe(Character),
		Character->IsLocallyControlled(),
		Character->HasAuthority(),
		Character->IsPlayerControlled(),
		bUseMovementComponent,
		NewState.bRootMotionEnabled,
		NewState.bMovementInputSuppressed,
		NewState.bCanBlendMontage,
		NewState.bShouldBlendLowerBody,
		*Character->GetActorLocation().ToCompactString(),
		MoveComp ? *MoveComp->Velocity.ToCompactString() : TEXT("NoMoveComp"));

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

	TArray<UPrimitiveComponent*> OverlappingComponents;
	RootMotionCollisionProbeComponent->GetOverlappingComponents(OverlappingComponents);

	
	for (UPrimitiveComponent* OverlappingComponent : OverlappingComponents)
	{
		ACharacter* OtherCharacter = Cast<ACharacter>(OverlappingComponent ? OverlappingComponent->GetOwner() : nullptr);
		const UCapsuleComponent* OtherCapsule = OtherCharacter ? OtherCharacter->GetCapsuleComponent() : nullptr;
		if (OtherCapsule && OverlappingComponent == OtherCapsule)
		{
			AddRootMotionCollisionCharacter(OtherCharacter);
		}
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

bool USyncAbilityMotionComponent::HasFallbackRootMotionBlockingCharacterCollision(
	float RequiredDot,
	float GraceRequiredDot,
	float& OutAngle,
	float& OutDot,
	ACharacter*& OutCharacter) const
{
	OutAngle = 0.f;
	OutDot = -1.f;
	OutCharacter = nullptr;

	const ACharacter* Character = GetOwnerCharacter();
	const UWorld* World = GetWorld();
	const UCapsuleComponent* OwnerCapsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Character || !World || !OwnerCapsule)
	{
		return false;
	}

	FVector Forward = Character->GetActorForwardVector();
	Forward.Z = 0.f;
	if (!Forward.Normalize())
	{
		return false;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SyncAbilityMotionRootMotionFallback), false, Character);
	QueryParams.AddIgnoredActor(Character);

	const float OwnerRadius = OwnerCapsule->GetUnscaledCapsuleRadius();
	const float ManualProbeDistance = FMath::Max(RootMotionCollisionProbeDistance, RootMotionCollisionFallbackProbeDistance);
	const float HalfProbeDistance = ManualProbeDistance * 0.5f;
	const FCollisionShape ProbeShape = FCollisionShape::MakeCapsule(
		OwnerRadius + HalfProbeDistance + SyncAbilityMotionCollisionProbe::ForwardContactPadding,
		OwnerCapsule->GetUnscaledCapsuleHalfHeight());
	const FVector ProbeLocation = Character->GetActorLocation() + Forward * HalfProbeDistance;

	const bool bFoundAnyOverlap = World->OverlapMultiByChannel(
		Overlaps,
		ProbeLocation,
		FQuat::Identity,
		ECC_Pawn,
		ProbeShape,
		QueryParams);

	if (!bFoundAnyOverlap)
	{
		return false;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACharacter* OtherCharacter = Cast<ACharacter>(Overlap.GetActor());
		UCapsuleComponent* OtherCapsule = OtherCharacter ? OtherCharacter->GetCapsuleComponent() : nullptr;
		if (!OtherCharacter || OtherCharacter == Character || !OtherCapsule || Overlap.GetComponent() != OtherCapsule)
		{
			continue;
		}

		if (!SyncAbilityMotionCollisionProbe::IsForwardContactBlocking(
			Character,
			OtherCharacter,
			ManualProbeDistance,
			RequiredDot,
			GraceRequiredDot,
			true,
			OutAngle,
			OutDot))
		{
			continue;
		}

		OutCharacter = OtherCharacter;
		return true;
	}

	return false;
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