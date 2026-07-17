#include "AbilityMotion/Component/PP_AbilityMotionComponent.h"

#include "AnimInstance/PP_AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Diagnostics/PP_NetMotionDiagnostics.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

namespace PPAbilityMotionCollisionProbe
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
			PPAbilityMotionCollisionProbe::ContactCenterAngleToleranceDegrees,
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

UPP_AbilityMotionComponent::UPP_AbilityMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPP_AbilityMotionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UPP_AbilityMotionComponent, AbilityMotionState, COND_SkipOwner);
}

void UPP_AbilityMotionComponent::SetAbilityMotionState(const FPP_AbilityMotionState& NewState)
{
	if (AbilityMotionState == NewState) return;

	AbilityMotionState = NewState;
	ApplyAbilityMotionState(NewState);

	ACharacter* Character = GetOwnerCharacter();
	if (Character && !Character->HasAuthority())
	{
		// The owner predicts immediately; the server replicates the state to proxies.
		ServerSetAbilityMotionState(NewState);
	}
}

void UPP_AbilityMotionComponent::ResetAbilityMotionState()
{
	ClearRootMotionCollisionProbe();

	if (ACharacter* Character = GetOwnerCharacter())
	{
		if (UPP_CharacterMovementComponent* MoveComp =
			Cast<UPP_CharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			MoveComp->SetAbilityRootMotionSuppressed(false);
			MoveComp->SetAbilityMovementInputSuppressed(false);
		}
	}

	FPP_AbilityMotionState DefaultState;
	DefaultState.bCanBlendMontage = false;
	DefaultState.bShouldBlendLowerBody = false;
	DefaultState.bRootMotionEnabled = true;
	DefaultState.bMovementInputSuppressed = false;

	SetAbilityMotionState(DefaultState);
}

void UPP_AbilityMotionComponent::SetServerMovementCorrectionIgnoreForAbility(bool bEnabled)
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

void UPP_AbilityMotionComponent::ServerSetServerMovementCorrectionIgnoreForAbility_Implementation(bool bEnabled)
{
	ApplyServerMovementCorrectionIgnoreForAbility(bEnabled);
}

void UPP_AbilityMotionComponent::ApplyServerMovementCorrectionIgnoreForAbility(bool bEnabled)
{
	ACharacter* Character = GetOwnerCharacter();
	UCharacterMovementComponent* MoveComp = Character ? Character->GetCharacterMovement() : nullptr;
	if (!MoveComp)
	{
		return;
	}

	if (bEnabled)
	{
		if (!bHasSavedServerMovementCorrectionIgnore)
		{
			bSavedServerIgnoreClientMovementErrorChecksAndCorrection =
				MoveComp->bIgnoreClientMovementErrorChecksAndCorrection;
			bSavedServerAcceptClientAuthoritativePosition =
				MoveComp->bServerAcceptClientAuthoritativePosition;
			bHasSavedServerMovementCorrectionIgnore = true;
		}

		// Server half of the explicit per-ability opt-in: skip normal error correction and copy
		// the autonomous client's reported position. Both flags are required; suppressing errors
		// alone would leave the server on a potentially different character-collision result.
		MoveComp->bIgnoreClientMovementErrorChecksAndCorrection = true;
		MoveComp->bServerAcceptClientAuthoritativePosition = true;
		return;
	}

	if (!bHasSavedServerMovementCorrectionIgnore)
	{
		return;
	}

	MoveComp->bIgnoreClientMovementErrorChecksAndCorrection =
		bSavedServerIgnoreClientMovementErrorChecksAndCorrection;
	MoveComp->bServerAcceptClientAuthoritativePosition =
		bSavedServerAcceptClientAuthoritativePosition;
	bHasSavedServerMovementCorrectionIgnore = false;
}

void UPP_AbilityMotionComponent::ConfigureRootMotionCollisionProbe(
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

	// Reuse one attached capsule and update it only when probe settings change.
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

void UPP_AbilityMotionComponent::ClearRootMotionCollisionProbe()
{
	if (bLastRootMotionCollisionBlocked)
	{
		UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
			TEXT("[RootMotionCharacterCollisionChanged] Owner={%s} Blocked=0 Blocker={%s} Reason=ProbeCleared"),
			*PP_GetNetMotionActorContext(GetOwnerCharacter()),
			*PP_GetNetMotionActorContext(LastRootMotionCollisionBlockingCharacter.Get()));
	}

	bRootMotionCollisionProbeEnabled = false;
	bLastRootMotionCollisionBlocked = false;
	LastRootMotionCollisionBlockTimeSeconds = -1000.f;
	LastRootMotionCollisionBlockingCharacter.Reset();
	RootMotionCollisionProbeDistance = 0.f;
	RootMotionCollisionFallbackProbeDistance = 0.f;
	RootMotionCollisionCharacters.Reset();

	if (RootMotionCollisionProbeComponent)
	{
		RootMotionCollisionProbeComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

bool UPP_AbilityMotionComponent::HasRootMotionBlockingCharacterCollision()
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
		RootMotionCollisionForwardAngleDegrees + PPAbilityMotionCollisionProbe::AngleReleaseGraceDegrees,
		0.f,
		180.f);
	const float GraceRequiredDot = FMath::Cos(FMath::DegreesToRadians(GraceAngleDegrees));

	// Prefer current probe overlaps and remove stale entries as they are checked.
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
			bLastRootMotionCollisionBlocked &&
			Dot >= GraceRequiredDot;

		float ContactAngle = 0.f;
		float ContactDot = -1.f;
		const bool bContactBlock = PPAbilityMotionCollisionProbe::IsForwardContactBlocking(
			Character,
			OtherCharacter,
			RootMotionCollisionProbeDistance,
			RequiredDot,
			GraceRequiredDot,
			bLastRootMotionCollisionBlocked,
			ContactAngle,
			ContactDot);

		if (bStrictAngleBlock || bAngleGraceBlock || bContactBlock)
		{
			if (!bLastRootMotionCollisionBlocked ||
				LastRootMotionCollisionBlockingCharacter.Get() != OtherCharacter)
			{
				const FVector OwnerLocation = Character->GetActorLocation();
				const FVector OtherLocation = OtherCharacter->GetActorLocation();
				const FVector ToOtherRaw = FVector(
					OtherLocation.X - OwnerLocation.X, OtherLocation.Y - OwnerLocation.Y, 0.f);
				const float CenterDistance = ToOtherRaw.Size2D();
				const UCapsuleComponent* OwnerCapsule = Character->GetCapsuleComponent();
				const float CombinedRadius =
					(OwnerCapsule ? OwnerCapsule->GetScaledCapsuleRadius() : 0.f) +
					OtherCapsule->GetScaledCapsuleRadius();
				const float SurfaceDistance = FMath::Max(0.f, CenterDistance - CombinedRadius);
				const TCHAR* Source = bContactBlock ? TEXT("ForwardContact") :
					(bStrictAngleBlock ? TEXT("ProbeOverlap") : TEXT("AngleGrace"));
				UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
					TEXT("[RootMotionCharacterCollisionChanged] Owner={%s} Blocked=1 Blocker={%s} Source=%s CenterDistance=%.2f SurfaceDistance=%.2f ForwardDistance=%.2f Angle=%.2f Dot=%.3f ProbeDistance=%.2f FallbackDistance=%.2f Strict=%d Contact=%d"),
					*PP_GetNetMotionActorContext(Character),
					*PP_GetNetMotionActorContext(OtherCharacter), Source,
					CenterDistance, SurfaceDistance, FVector::DotProduct(ToOtherRaw, Forward),
					Angle, Dot, RootMotionCollisionProbeDistance,
					RootMotionCollisionFallbackProbeDistance,
					bStrictAngleBlock ? 1 : 0, bContactBlock ? 1 : 0);
			}

			bLastRootMotionCollisionBlocked = true;
			LastRootMotionCollisionBlockTimeSeconds = NowSeconds;
			LastRootMotionCollisionBlockingCharacter = OtherCharacter;
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
		bLastRootMotionCollisionBlocked &&
		SecondsSinceBlock >= 0.f &&
		SecondsSinceBlock <= PPAbilityMotionCollisionProbe::LostOverlapGraceSeconds;

	if (bLostOverlapGraceBlock)
	{
		// Brief grace prevents a one-frame overlap loss from restarting root motion.
		return true;
	}

	float FallbackAngle = 0.f;
	float FallbackDot = -1.f;
	ACharacter* FallbackCharacter = nullptr;
	if (bLastRootMotionCollisionBlocked &&
		HasFallbackRootMotionBlockingCharacterCollision(RequiredDot, GraceRequiredDot, FallbackAngle, FallbackDot, FallbackCharacter))
	{
		// Manual overlap is a backup when component overlap events arrive late.
		bLastRootMotionCollisionBlocked = true;
		LastRootMotionCollisionBlockTimeSeconds = NowSeconds;
		LastRootMotionCollisionBlockingCharacter = FallbackCharacter;
		return true;
	}

	if (bLastRootMotionCollisionBlocked)
	{
		UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
			TEXT("[RootMotionCharacterCollisionChanged] Owner={%s} Blocked=0 Blocker={%s} Reason=NoBlockingCharacter SecondsSinceLastBlock=%.3f RejectedCharacter=%s RejectedAngle=%.2f RejectedDot=%.3f"),
			*PP_GetNetMotionActorContext(GetOwnerCharacter()),
			*PP_GetNetMotionActorContext(LastRootMotionCollisionBlockingCharacter.Get()),
			SecondsSinceBlock, *GetNameSafe(BestRejectedCharacter), BestRejectedAngle, BestRejectedDot);
	}
	bLastRootMotionCollisionBlocked = false;
	LastRootMotionCollisionBlockingCharacter.Reset();
	return false;
}

void UPP_AbilityMotionComponent::ServerSetAbilityMotionState_Implementation(const FPP_AbilityMotionState& NewState)
{
	if (AbilityMotionState == NewState) return;

	AbilityMotionState = NewState;
	ApplyAbilityMotionState(NewState);
}

void UPP_AbilityMotionComponent::OnRep_AbilityMotionState()
{
	const ACharacter* Character = GetOwnerCharacter();
	if (Character && Character->IsLocallyControlled())
	{
		return;
	}

	ApplyAbilityMotionState(AbilityMotionState);
}

void UPP_AbilityMotionComponent::OnRootMotionCollisionProbeBeginOverlap(
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

void UPP_AbilityMotionComponent::OnRootMotionCollisionProbeEndOverlap(
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

void UPP_AbilityMotionComponent::ApplyAbilityMotionState(const FPP_AbilityMotionState& NewState)
{
	ACharacter* Character = GetOwnerCharacter();
	if (!Character) return;

	USkeletalMeshComponent* MeshComp = Character->GetMesh();
	if (!MeshComp) return;

	UPP_AnimInstance* AnimInstance = Cast<UPP_AnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	AnimInstance->bCanBlendMontage = NewState.bCanBlendMontage;
	AnimInstance->bShouldBlendLowerBody = NewState.bShouldBlendLowerBody;

	UPP_CharacterMovementComponent* MoveComp =
		Cast<UPP_CharacterMovementComponent>(Character->GetCharacterMovement());
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

ACharacter* UPP_AbilityMotionComponent::GetOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

void UPP_AbilityMotionComponent::EnsureRootMotionCollisionProbe()
{
	if (RootMotionCollisionProbeComponent) return;

	ACharacter* Character = GetOwnerCharacter();
	UCapsuleComponent* OwnerCapsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Character || !OwnerCapsule) return;

	RootMotionCollisionProbeComponent = NewObject<UCapsuleComponent>(Character, TEXT("PPAbilityMotionRootMotionCollisionProbe"));
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

void UPP_AbilityMotionComponent::RebuildRootMotionCollisionOverlaps()
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

bool UPP_AbilityMotionComponent::IsRootMotionCollisionCharacterInFront(const ACharacter* OtherCharacter) const
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

bool UPP_AbilityMotionComponent::HasFallbackRootMotionBlockingCharacterCollision(
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
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PPAbilityMotionRootMotionFallback), false, Character);
	QueryParams.AddIgnoredActor(Character);

	const float OwnerRadius = OwnerCapsule->GetUnscaledCapsuleRadius();
	const float ManualProbeDistance = FMath::Max(RootMotionCollisionProbeDistance, RootMotionCollisionFallbackProbeDistance);
	const float HalfProbeDistance = ManualProbeDistance * 0.5f;
	const FCollisionShape ProbeShape = FCollisionShape::MakeCapsule(
		OwnerRadius + HalfProbeDistance + PPAbilityMotionCollisionProbe::ForwardContactPadding,
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

		if (!PPAbilityMotionCollisionProbe::IsForwardContactBlocking(
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

void UPP_AbilityMotionComponent::AddRootMotionCollisionCharacter(ACharacter* OtherCharacter)
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

void UPP_AbilityMotionComponent::RemoveRootMotionCollisionCharacter(ACharacter* OtherCharacter)
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

