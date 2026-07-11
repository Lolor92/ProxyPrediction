#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"

#include "AnimInstance/PP_AnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

namespace PPAbilityMotionFlags
{
	// Custom saved-move bits reproduce ability locks during server resimulation.
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

class FSavedMove_PPAbilityMotion final : public FSavedMove_Character
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
		// Send the two ability locks with the normal Character Movement move.
		uint8 Result = Super::GetCompressedFlags();

		if (bSavedAbilityRootMotionSuppressed)
		{
			Result |= PPAbilityMotionFlags::SuppressAbilityRootMotion;
		}

		if (bSavedAbilityMovementInputSuppressed)
		{
			Result |= PPAbilityMotionFlags::SuppressAbilityMovementInput;
		}

		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const FSavedMove_PPAbilityMotion* NewPPMove =
			static_cast<const FSavedMove_PPAbilityMotion*>(NewMove.Get());

		if (bSavedAbilityRootMotionSuppressed != NewPPMove->bSavedAbilityRootMotionSuppressed)
		{
			return false;
		}

		if (bSavedAbilityMovementInputSuppressed != NewPPMove->bSavedAbilityMovementInputSuppressed)
		{
			return false;
		}

		return Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

		if (const UPP_CharacterMovementComponent* MoveComp =
			Cast<UPP_CharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			bSavedAbilityRootMotionSuppressed = MoveComp->IsAbilityRootMotionSuppressed();
			bSavedAbilityMovementInputSuppressed = MoveComp->IsAbilityMovementInputSuppressed();
		}
	}

private:
	uint8 bSavedAbilityRootMotionSuppressed : 1 = false;
	uint8 bSavedAbilityMovementInputSuppressed : 1 = false;
};

class FNetworkPredictionData_Client_PPAbilityMotion final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_PPAbilityMotion(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_PPAbilityMotion());
	}
};

void UPP_CharacterMovementComponent::SetAbilityRootMotionSuppressed(bool bInSuppressed)
{
	if (bAbilityRootMotionSuppressed == bInSuppressed) return;

	bAbilityRootMotionSuppressed = bInSuppressed;
	RefreshAbilityRootMotionMode();
}

void UPP_CharacterMovementComponent::RefreshAbilityRootMotionMode()
{
	if (!CharacterOwner) return;

	USkeletalMeshComponent* MeshComp = CharacterOwner->GetMesh();
	if (!MeshComp) return;

	UPP_AnimInstance* AnimInstance = Cast<UPP_AnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	const bool bLocalOwnerReaction = bIgnoreServerRootMotionMontageTrackCorrection && CharacterOwner->IsLocallyControlled();
	const bool bRootMotionEnabled = bLocalOwnerReaction || !bAbilityRootMotionSuppressed;
	AnimInstance->bRootMotionEnabled = bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(bRootMotionEnabled
		? ERootMotionMode::RootMotionFromMontagesOnly
		: ERootMotionMode::IgnoreRootMotion);
}

void UPP_CharacterMovementComponent::SetAbilityMovementInputSuppressed(bool bInSuppressed)
{
	bAbilityMovementInputSuppressed = bInSuppressed;
}

void UPP_CharacterMovementComponent::SetCrowdControlMovementInputSuppressed(const bool bInSuppressed)
{
	bCrowdControlMovementInputSuppressed = bInSuppressed;
}

void UPP_CharacterMovementComponent::SetIgnoreServerRootMotionMontageTrackCorrection(bool bInIgnore)
{
	if (bIgnoreServerRootMotionMontageTrackCorrection == bInIgnore)
	{
		return;
	}

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

void UPP_CharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
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
			if (UPP_AnimInstance* PPAnimInstance = Cast<UPP_AnimInstance>(AnimInstance))
			{
				PPAnimInstance->bRootMotionEnabled = true;
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

void UPP_CharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// Remote/server simulation restores the exact locks saved by the owning client.
	const bool bIsLocallyControlled = CharacterOwner && CharacterOwner->IsLocallyControlled();
	if (!bIsLocallyControlled)
	{
		SetAbilityRootMotionSuppressed((Flags & PPAbilityMotionFlags::SuppressAbilityRootMotion) != 0);
		SetAbilityMovementInputSuppressed((Flags & PPAbilityMotionFlags::SuppressAbilityMovementInput) != 0);
	}
}

FNetworkPredictionData_Client* UPP_CharacterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);

	if (ClientPredictionData == nullptr)
	{
		UPP_CharacterMovementComponent* MutableThis =
			const_cast<UPP_CharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_PPAbilityMotion(*this);
	}

	return ClientPredictionData;
}

FVector UPP_CharacterMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration) const
{
	// Zero acceleration instead of blocking input events so ability release still works.
	if (bAbilityMovementInputSuppressed || bCrowdControlMovementInputSuppressed)
	{
		return FVector::ZeroVector;
	}

	return Super::ScaleInputAcceleration(InputAcceleration);
}

void UPP_CharacterMovementComponent::ClientAdjustRootMotionPosition_Implementation(
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

void UPP_CharacterMovementComponent::ClientAdjustRootMotionPosition_Implementation(
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
	if (bIgnoreServerRootMotionMontageTrackCorrection)
	{
		return;
	}

	Super::ClientAdjustRootMotionPosition_Implementation(TimeStamp, ServerMontageTrackPosition, ServerLoc,
		ServerRotation, ServerVelZ, ServerMovementBaseInterfaceData, ServerBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode);
}

void UPP_CharacterMovementComponent::ClientAdjustRootMotionSourcePosition_Implementation(
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

void UPP_CharacterMovementComponent::ClientAdjustRootMotionSourcePosition_Implementation(
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
	if (bIgnoreServerRootMotionMontageTrackCorrection && bHasAnimRootMotion)
	{
		return;
	}

	Super::ClientAdjustRootMotionSourcePosition_Implementation(TimeStamp, ServerRootMotion, bHasAnimRootMotion,
		ServerMontageTrackPosition, ServerLoc, ServerRotation, ServerVelZ, ServerMovementBaseInterfaceData,
		ServerBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

