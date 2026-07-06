#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"

#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "Misc/OutputDevice.h"

namespace SyncAbilityMotionFlags
{
	constexpr uint8 SuppressAbilityRootMotion = FSavedMove_Character::FLAG_Custom_0;
	constexpr uint8 SuppressAbilityMovementInput = FSavedMove_Character::FLAG_Custom_1;
}

namespace
{
	class FScopedIgnoreMontageTrackCorrection
	{
	public:
		FScopedIgnoreMontageTrackCorrection(ACharacter* Character, bool bEnabled)
		{
			if (!bEnabled || !Character) return;

			MontageInstance = Character->GetRootMotionAnimMontageInstance();
			if (MontageInstance)
			{
				MontageInstance->PushDisableRootMotion();
			}
		}

		~FScopedIgnoreMontageTrackCorrection()
		{
			if (MontageInstance)
			{
				MontageInstance->PopDisableRootMotion();
			}
		}

	private:
		FAnimMontageInstance* MontageInstance = nullptr;
	};
}

class FSavedMove_SyncAbilityMotion final : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	virtual void Clear() override
	{
		Super::Clear();

		bSavedAbilityRootMotionSuppressed = false;
		bSavedAbilityMovementInputSuppressed = false;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = Super::GetCompressedFlags();

		if (bSavedAbilityRootMotionSuppressed)
		{
			Result |= SyncAbilityMotionFlags::SuppressAbilityRootMotion;
		}

		if (bSavedAbilityMovementInputSuppressed)
		{
			Result |= SyncAbilityMotionFlags::SuppressAbilityMovementInput;
		}

		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const FSavedMove_SyncAbilityMotion* NewSyncMove =
			static_cast<const FSavedMove_SyncAbilityMotion*>(NewMove.Get());

		if (bSavedAbilityRootMotionSuppressed != NewSyncMove->bSavedAbilityRootMotionSuppressed)
		{
			return false;
		}

		if (bSavedAbilityMovementInputSuppressed != NewSyncMove->bSavedAbilityMovementInputSuppressed)
		{
			return false;
		}

		return Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
	                        FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

		if (const USyncAbilityMotionCharacterMovementComponent* MoveComp =
			Cast<USyncAbilityMotionCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			bSavedAbilityRootMotionSuppressed = MoveComp->IsAbilityRootMotionSuppressed();
			bSavedAbilityMovementInputSuppressed = MoveComp->IsAbilityMovementInputSuppressed();
		}
	}

private:
	uint8 bSavedAbilityRootMotionSuppressed : 1 = false;
	uint8 bSavedAbilityMovementInputSuppressed : 1 = false;
};

class FNetworkPredictionData_Client_SyncAbilityMotion final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_SyncAbilityMotion(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_SyncAbilityMotion());
	}
};

void USyncAbilityMotionCharacterMovementComponent::SetAbilityRootMotionSuppressed(bool bInSuppressed)
{
	if (bAbilityRootMotionSuppressed == bInSuppressed) return;

	UE_LOG(LogTemp, Warning,
	       TEXT("SAM_RM_SUPPRESS Owner=%s Local=%d Auth=%d Old=%d New=%d Loc=%s Vel=%s"),
	       *GetNameSafe(CharacterOwner),
	       CharacterOwner ? CharacterOwner->IsLocallyControlled() : false,
	       CharacterOwner ? CharacterOwner->HasAuthority() : false,
	       bAbilityRootMotionSuppressed,
	       bInSuppressed,
	       CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
	       *Velocity.ToCompactString());


	if (CharacterOwner && CharacterOwner->IsLocallyControlled() && bInSuppressed)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SAM_RM_SUPPRESS_CALLSTACK Owner=%s RootMotion is being suppressed on the local owner"),
			*GetNameSafe(CharacterOwner));

		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}

	bAbilityRootMotionSuppressed = bInSuppressed;
	RefreshAbilityRootMotionMode();
}

void USyncAbilityMotionCharacterMovementComponent::RefreshAbilityRootMotionMode()
{
	if (!CharacterOwner) return;

	USkeletalMeshComponent* MeshComp = CharacterOwner->GetMesh();
	if (!MeshComp) return;

	USyncAbilityMotionAnimInstance* AnimInstance =
		Cast<USyncAbilityMotionAnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	const bool bLocalOwnerReaction = bIgnoreServerRootMotionMontageTrackCorrection && CharacterOwner->IsLocallyControlled();
	const bool bRootMotionEnabled = bLocalOwnerReaction || !bAbilityRootMotionSuppressed;
	AnimInstance->bRootMotionEnabled = bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(bRootMotionEnabled
		                                ? ERootMotionMode::RootMotionFromMontagesOnly
		                                : ERootMotionMode::IgnoreRootMotion);

	UE_LOG(LogTemp, Warning,
	       TEXT("SAM_RM_MODE Owner=%s Local=%d Auth=%d Suppressed=%d IgnoreTrackCorrection=%d LocalOwnerReaction=%d RootMotionEnabled=%d AnimRootMotionMode=%d Loc=%s Vel=%s"),
	       *GetNameSafe(CharacterOwner),
	       CharacterOwner->IsLocallyControlled(),
	       CharacterOwner->HasAuthority(),
	       bAbilityRootMotionSuppressed,
	       bIgnoreServerRootMotionMontageTrackCorrection,
	       bLocalOwnerReaction,
	       bRootMotionEnabled,
	       AnimInstance->RootMotionMode.GetValue(),
	       *CharacterOwner->GetActorLocation().ToCompactString(),
	       *Velocity.ToCompactString());
}

void USyncAbilityMotionCharacterMovementComponent::SetAbilityMovementInputSuppressed(bool bInSuppressed)
{
	bAbilityMovementInputSuppressed = bInSuppressed;
}

void USyncAbilityMotionCharacterMovementComponent::SetIgnoreServerRootMotionMontageTrackCorrection(bool bInIgnore)
{
	if (bIgnoreServerRootMotionMontageTrackCorrection == bInIgnore)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
	       TEXT("SAM_IGNORE_RM_TRACK_CORRECTION Owner=%s Local=%d Auth=%d Old=%d New=%d ClientIgnoreCorrections=%d IgnoreErrorChecks=%d Loc=%s Vel=%s"),
	       *GetNameSafe(CharacterOwner),
	       CharacterOwner ? CharacterOwner->IsLocallyControlled() : false,
	       CharacterOwner ? CharacterOwner->HasAuthority() : false,
	       bIgnoreServerRootMotionMontageTrackCorrection,
	       bInIgnore,
	       bClientIgnoreMovementCorrections,
	       bIgnoreClientMovementErrorChecksAndCorrection,
	       CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
	       *Velocity.ToCompactString());

	const bool bLocalOwnerReaction = CharacterOwner && CharacterOwner->IsLocallyControlled();

	if (bInIgnore && bLocalOwnerReaction)
	{
		if (!bHasSavedOwnerReactionCorrectionFlags)
		{
			bSavedOwnerReactionClientIgnoreMovementCorrections = bClientIgnoreMovementCorrections;
			bSavedOwnerReactionIgnoreErrorChecksAndCorrection = bIgnoreClientMovementErrorChecksAndCorrection;
			bHasSavedOwnerReactionCorrectionFlags = true;
		}

		bClientIgnoreMovementCorrections = true;
		bIgnoreClientMovementErrorChecksAndCorrection = true;
	}
	else if (!bInIgnore && bHasSavedOwnerReactionCorrectionFlags)
	{
		bClientIgnoreMovementCorrections = bSavedOwnerReactionClientIgnoreMovementCorrections;
		bIgnoreClientMovementErrorChecksAndCorrection = bSavedOwnerReactionIgnoreErrorChecksAndCorrection;
		bHasSavedOwnerReactionCorrectionFlags = false;
	}

	bIgnoreServerRootMotionMontageTrackCorrection = bInIgnore;
	bHasOwnerReactionTraceLocation = false;
	LastOwnerReactionTraceLocation = CharacterOwner ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
	RefreshAbilityRootMotionMode();
}

void USyncAbilityMotionCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	const bool bIsLocallyControlled = CharacterOwner && CharacterOwner->IsLocallyControlled();
	if (!bIsLocallyControlled)
	{
		SetAbilityRootMotionSuppressed((Flags & SyncAbilityMotionFlags::SuppressAbilityRootMotion) != 0);
		SetAbilityMovementInputSuppressed((Flags & SyncAbilityMotionFlags::SuppressAbilityMovementInput) != 0);
	}
}

FNetworkPredictionData_Client* USyncAbilityMotionCharacterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);

	if (ClientPredictionData == nullptr)
	{
		USyncAbilityMotionCharacterMovementComponent* MutableThis =
			const_cast<USyncAbilityMotionCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_SyncAbilityMotion(*this);
	}

	return ClientPredictionData;
}


// SAM_DIAGNOSTIC_HANDLE_IMPACT_IMPL
void USyncAbilityMotionCharacterMovementComponent::HandleImpact(
const FHitResult& Hit,
float TimeSlice,
const FVector& MoveDelta)
{
const bool bLocalOwner = CharacterOwner && CharacterOwner->IsLocallyControlled();
const bool bShouldLog =
bLocalOwner &&
(
Velocity.Size2D() > 600.f ||
MoveDelta.Size2D() > 20.f ||
Hit.bStartPenetrating ||
Hit.PenetrationDepth > KINDA_SMALL_NUMBER
);

if (bShouldLog)
{
UAnimInstance* AnimInstance =
CharacterOwner && CharacterOwner->GetMesh()
? CharacterOwner->GetMesh()->GetAnimInstance()
: nullptr;

UE_LOG(LogTemp, Warning,
TEXT("SAM_IMPACT Owner=%s Local=%d Auth=%d HitActor=%s HitComp=%s Blocking=%d StartPen=%d PenDepth=%.2f Time=%.3f TimeSlice=%.4f MoveDelta=%s MoveDelta2D=%.2f ImpactPoint=%s ImpactNormal=%s Normal=%s Loc=%s Vel=%s Speed2D=%.2f MoveMode=%d RootMotionMode=%d RM_Suppressed=%d InputSuppressed=%d"),
*GetNameSafe(CharacterOwner),
CharacterOwner ? CharacterOwner->IsLocallyControlled() : false,
CharacterOwner ? CharacterOwner->HasAuthority() : false,
*GetNameSafe(Hit.GetActor()),
*GetNameSafe(Hit.GetComponent().Get()),
Hit.bBlockingHit,
Hit.bStartPenetrating,
Hit.PenetrationDepth,
Hit.Time,
TimeSlice,
*MoveDelta.ToCompactString(),
MoveDelta.Size2D(),
*Hit.ImpactPoint.ToCompactString(),
*Hit.ImpactNormal.ToCompactString(),
*Hit.Normal.ToCompactString(),
CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
*Velocity.ToCompactString(),
Velocity.Size2D(),
MovementMode,
AnimInstance ? AnimInstance->RootMotionMode.GetValue() : -1,
bAbilityRootMotionSuppressed,
bAbilityMovementInputSuppressed);
}

Super::HandleImpact(Hit, TimeSlice, MoveDelta);
}

FVector USyncAbilityMotionCharacterMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration) const
{
	if (bAbilityMovementInputSuppressed)
	{
		return FVector::ZeroVector;
	}

	return Super::ScaleInputAcceleration(InputAcceleration);
}

void USyncAbilityMotionCharacterMovementComponent::ClientAdjustRootMotionPosition_Implementation(
	float TimeStamp,
	float ServerMontageTrackPosition,
	FVector ServerLoc,
	FVector_NetQuantizeNormal ServerRotation,
	float ServerVelZ,
	UPrimitiveComponent* ServerBase,
	FName ServerBoneName,
	bool bHasBase,
	bool bBaseRelativePosition,
	uint8 ServerMovementMode)
{
	FMovementBaseInterfaceData ServerMovementBaseInterfaceData(ServerBase);
	ClientAdjustRootMotionPosition_Implementation(TimeStamp, ServerMontageTrackPosition, ServerLoc, ServerRotation,
	                                              ServerVelZ, &ServerMovementBaseInterfaceData, ServerBoneName, bHasBase, bBaseRelativePosition,
	                                              ServerMovementMode);
}

void USyncAbilityMotionCharacterMovementComponent::ClientAdjustRootMotionPosition_Implementation(
	float TimeStamp,
	float ServerMontageTrackPosition,
	FVector ServerLoc,
	FVector_NetQuantizeNormal ServerRotation,
	float ServerVelZ,
	FMovementBaseInterfaceData* ServerMovementBaseInterfaceData,
	FName ServerBoneName,
	bool bHasBase,
	bool bBaseRelativePosition,
	uint8 ServerMovementMode)
{
	UE_LOG(LogTemp, Warning,
	       TEXT("SAM_ROOTMOTION_CORRECTION Owner=%s Local=%d Auth=%d Ignore=%d TimeStamp=%.3f Track=%.3f ServerLoc=%s ClientLocBefore=%s Dist2D=%.2f ServerVelZ=%.2f MoveMode=%d Vel=%s"),
	       *GetNameSafe(CharacterOwner),
	       CharacterOwner ? CharacterOwner->IsLocallyControlled() : false,
	       CharacterOwner ? CharacterOwner->HasAuthority() : false,
	       bIgnoreServerRootMotionMontageTrackCorrection,
	       TimeStamp,
	       ServerMontageTrackPosition,
	       *ServerLoc.ToCompactString(),
	       CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
	       CharacterOwner ? FVector::Dist2D(ServerLoc, CharacterOwner->GetActorLocation()) : -1.f,
	       ServerVelZ,
	       ServerMovementMode,
	       *Velocity.ToCompactString());

	if (bIgnoreServerRootMotionMontageTrackCorrection)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("SAM_ROOTMOTION_CORRECTION_SKIPPED Owner=%s Reason=IgnoreTrackCorrection"),
		       *GetNameSafe(CharacterOwner));
		return;
	}

	const FVector ClientLocBeforeCorrection = CharacterOwner ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;

	Super::ClientAdjustRootMotionPosition_Implementation(TimeStamp, ServerMontageTrackPosition, ServerLoc,
	                                                     ServerRotation, ServerVelZ, ServerMovementBaseInterfaceData, ServerBoneName, bHasBase,
	                                                     bBaseRelativePosition, ServerMovementMode);

	UE_LOG(LogTemp, Warning,
	       TEXT("SAM_ROOTMOTION_CORRECTION_AFTER Owner=%s LocBefore=%s LocAfter=%s AppliedDelta2D=%.2f VelAfter=%s"),
	       *GetNameSafe(CharacterOwner),
	       *ClientLocBeforeCorrection.ToCompactString(),
	       CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
	       CharacterOwner ? FVector::Dist2D(ClientLocBeforeCorrection, CharacterOwner->GetActorLocation()) : -1.f,
	       *Velocity.ToCompactString());
}

void USyncAbilityMotionCharacterMovementComponent::ClientAdjustRootMotionSourcePosition_Implementation(
	float TimeStamp,
	FRootMotionSourceGroup ServerRootMotion,
	bool bHasAnimRootMotion,
	float ServerMontageTrackPosition,
	FVector ServerLoc,
	FVector_NetQuantizeNormal ServerRotation,
	float ServerVelZ,
	UPrimitiveComponent* ServerBase,
	FName ServerBoneName,
	bool bHasBase,
	bool bBaseRelativePosition,
	uint8 ServerMovementMode)
{
	FMovementBaseInterfaceData ServerMovementBaseInterfaceData(ServerBase);
	ClientAdjustRootMotionSourcePosition_Implementation(TimeStamp, ServerRootMotion, bHasAnimRootMotion,
	                                                    ServerMontageTrackPosition, ServerLoc, ServerRotation, ServerVelZ, &ServerMovementBaseInterfaceData,
	                                                    ServerBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

void USyncAbilityMotionCharacterMovementComponent::ClientAdjustRootMotionSourcePosition_Implementation(
	float TimeStamp,
	FRootMotionSourceGroup ServerRootMotion,
	bool bHasAnimRootMotion,
	float ServerMontageTrackPosition,
	FVector ServerLoc,
	FVector_NetQuantizeNormal ServerRotation,
	float ServerVelZ,
	FMovementBaseInterfaceData* ServerMovementBaseInterfaceData,
	FName ServerBoneName,
	bool bHasBase,
	bool bBaseRelativePosition,
	uint8 ServerMovementMode)
{
	UE_LOG(LogTemp, Warning,
	       TEXT(
		       "SAM_ROOTMOTION_SOURCE_CORRECTION Owner=%s Local=%d Auth=%d Ignore=%d HasAnimRM=%d TimeStamp=%.3f Track=%.3f ServerLoc=%s ClientLocBefore=%s Dist2D=%.2f ServerVelZ=%.2f MoveMode=%d Vel=%s"
	       ),
	       *GetNameSafe(CharacterOwner),
	       CharacterOwner ? CharacterOwner->IsLocallyControlled() : false,
	       CharacterOwner ? CharacterOwner->HasAuthority() : false,
	       bIgnoreServerRootMotionMontageTrackCorrection,
	       bHasAnimRootMotion,
	       TimeStamp,
	       ServerMontageTrackPosition,
	       *ServerLoc.ToCompactString(),
	       CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
	       CharacterOwner ? FVector::Dist2D(ServerLoc, CharacterOwner->GetActorLocation()) : -1.f,
	       ServerVelZ,
	       ServerMovementMode,
	       *Velocity.ToCompactString());

	if (bIgnoreServerRootMotionMontageTrackCorrection && bHasAnimRootMotion)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("SAM_ROOTMOTION_SOURCE_CORRECTION_SKIPPED Owner=%s Reason=IgnoreTrackCorrectionAndHasAnimRM"),
		       *GetNameSafe(CharacterOwner));
		return;
	}

	const FVector ClientLocBeforeSourceCorrection = CharacterOwner ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;

	Super::ClientAdjustRootMotionSourcePosition_Implementation(TimeStamp, ServerRootMotion, bHasAnimRootMotion,
	                                                           ServerMontageTrackPosition, ServerLoc, ServerRotation, ServerVelZ, ServerMovementBaseInterfaceData,
	                                                           ServerBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);

	UE_LOG(LogTemp, Warning,
	       TEXT("SAM_ROOTMOTION_SOURCE_CORRECTION_AFTER Owner=%s LocBefore=%s LocAfter=%s AppliedDelta2D=%.2f VelAfter=%s"),
	       *GetNameSafe(CharacterOwner),
	       *ClientLocBeforeSourceCorrection.ToCompactString(),
	       CharacterOwner ? *CharacterOwner->GetActorLocation().ToCompactString() : TEXT("None"),
	       CharacterOwner ? FVector::Dist2D(ClientLocBeforeSourceCorrection, CharacterOwner->GetActorLocation()) : -1.f,
	       *Velocity.ToCompactString());
}
