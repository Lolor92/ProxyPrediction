#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"
#include "Diagnostics/PP_NetMotionDiagnostics.h"

#include "AnimInstance/PP_AnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/Component/PP_CombatComponent.h"
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
	// Root-motion responses refer to an older saved move. When the server track already matches
	// that move, applying its old timestamp to the currently advancing montage creates a visual
	// rewind without resolving any montage disagreement.
	constexpr float RootMotionTrackAgreementToleranceSeconds = 0.05f;

	struct FAnimRootMotionCorrectionContext
	{
		TWeakObjectPtr<UAnimMontage> SavedMoveMontage;
		TWeakObjectPtr<UAnimMontage> ActiveMontage;
		float SavedMoveTrackPosition = 0.f;
		bool bFoundSavedMove = false;
	};

	FAnimRootMotionCorrectionContext BuildAnimRootMotionCorrectionContext(
		UPP_CharacterMovementComponent* MovementComponent, const float TimeStamp)
	{
		FAnimRootMotionCorrectionContext Context;
		if (!MovementComponent)
		{
			return Context;
		}

		if (FNetworkPredictionData_Client_Character* ClientData =
			MovementComponent->GetPredictionData_Client_Character())
		{
			const int32 MoveIndex = ClientData->GetSavedMoveIndex(TimeStamp);
			if (ClientData->SavedMoves.IsValidIndex(MoveIndex) && ClientData->SavedMoves[MoveIndex].IsValid())
			{
				const FSavedMove_Character& SavedMove = *ClientData->SavedMoves[MoveIndex];
				Context.bFoundSavedMove = true;
				Context.SavedMoveMontage = SavedMove.RootMotionMontage;
				Context.SavedMoveTrackPosition = SavedMove.RootMotionTrackPosition;
			}
		}

		if (const ACharacter* Character = MovementComponent->GetCharacterOwner())
		{
			if (const FAnimMontageInstance* MontageInstance = Character->GetRootMotionAnimMontageInstance())
			{
				Context.ActiveMontage = MontageInstance->Montage;
			}
		}

		return Context;
	}

	bool IsCorrectionForDifferentActiveMontage(const FAnimRootMotionCorrectionContext& Context)
	{
		return Context.bFoundSavedMove &&
			Context.SavedMoveMontage.IsValid() &&
			Context.ActiveMontage.IsValid() &&
			Context.SavedMoveMontage != Context.ActiveMontage;
	}

	bool IsCorrectionTrackAlignedWithSavedMove(
		const FAnimRootMotionCorrectionContext& Context,
		const float ServerMontageTrackPosition)
	{
		return Context.bFoundSavedMove &&
			Context.SavedMoveMontage.IsValid() &&
			Context.ActiveMontage.IsValid() &&
			Context.SavedMoveMontage == Context.ActiveMontage &&
			FMath::Abs(ServerMontageTrackPosition - Context.SavedMoveTrackPosition) <=
				RootMotionTrackAgreementToleranceSeconds;
	}

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

	// This project engine's correction-ignore path still acknowledges the matching saved move.
	// Use it briefly for a response from an older montage so the response cannot displace the
	// current ability, while the acknowledgement continues to drain SavedMoves normally.
	class FScopedIgnoreClientMovementCorrection
	{
	public:
		FScopedIgnoreClientMovementCorrection(
			UPP_CharacterMovementComponent* InMovementComponent,
			const bool bEnabled)
			: MovementComponent(bEnabled ? InMovementComponent : nullptr)
		{
			if (MovementComponent)
			{
				bSavedIgnore = MovementComponent->bClientIgnoreMovementCorrections;
				MovementComponent->bClientIgnoreMovementCorrections = true;
			}
		}

		~FScopedIgnoreClientMovementCorrection()
		{
			if (MovementComponent)
			{
				MovementComponent->bClientIgnoreMovementCorrections = bSavedIgnore;
			}
		}

	private:
		UPP_CharacterMovementComponent* MovementComponent = nullptr;
		bool bSavedIgnore = false;
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

	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[RootMotionSuppressionChanged] %s Previous=%d New=%d IgnoreCorrections=%d ClientIgnore=%d ServerIgnoresError=%d"),
		*PP_GetNetMotionActorContext(CharacterOwner), bAbilityRootMotionSuppressed ? 1 : 0,
		bInSuppressed ? 1 : 0, bIgnoreServerRootMotionMontageTrackCorrection ? 1 : 0,
		bClientIgnoreMovementCorrections ? 1 : 0,
		bIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0);
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
	if (bAbilityMovementInputSuppressed == bInSuppressed) return;

	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[MovementInputSuppressionChanged] %s Source=Ability Previous=%d New=%d RootMotionSuppressed=%d CCSuppressed=%d"),
		*PP_GetNetMotionActorContext(CharacterOwner), bAbilityMovementInputSuppressed ? 1 : 0,
		bInSuppressed ? 1 : 0, bAbilityRootMotionSuppressed ? 1 : 0,
		bCrowdControlMovementInputSuppressed ? 1 : 0);
	bAbilityMovementInputSuppressed = bInSuppressed;
}

void UPP_CharacterMovementComponent::SetCrowdControlMovementInputSuppressed(const bool bInSuppressed)
{
	if (bCrowdControlMovementInputSuppressed == bInSuppressed) return;

	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[MovementInputSuppressionChanged] %s Source=CrowdControl Previous=%d New=%d AbilitySuppressed=%d RootMotionSuppressed=%d"),
		*PP_GetNetMotionActorContext(CharacterOwner), bCrowdControlMovementInputSuppressed ? 1 : 0,
		bInSuppressed ? 1 : 0, bAbilityMovementInputSuppressed ? 1 : 0,
		bAbilityRootMotionSuppressed ? 1 : 0);
	bCrowdControlMovementInputSuppressed = bInSuppressed;
}

void UPP_CharacterMovementComponent::SetIgnoreServerRootMotionMontageTrackCorrection(bool bInIgnore)
{
	if (bIgnoreServerRootMotionMontageTrackCorrection == bInIgnore)
	{
		return;
	}

	const bool bLocalOwnerReaction = CharacterOwner && CharacterOwner->IsLocallyControlled();
	const bool bOldClientIgnore = bClientIgnoreMovementCorrections;
	const bool bOldServerIgnore = bIgnoreClientMovementErrorChecksAndCorrection;

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
	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[CorrectionSuppressionChanged] %s Previous=%d New=%d LocalOwnerReaction=%d ClientIgnore=%d->%d ServerIgnoresError=%d->%d RootMotionSuppressed=%d"),
		*PP_GetNetMotionActorContext(CharacterOwner), bInIgnore ? 0 : 1, bInIgnore ? 1 : 0,
		bLocalOwnerReaction ? 1 : 0, bOldClientIgnore ? 1 : 0,
		bClientIgnoreMovementCorrections ? 1 : 0, bOldServerIgnore ? 1 : 0,
		bIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0,
		bAbilityRootMotionSuppressed ? 1 : 0);
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
}

float UPP_CharacterMovementComponent::GetMaxSpeed() const
{
	const float MaxSpeed = Super::GetMaxSpeed();
	if (MaxSpeed <= 0.f || !PawnOwner)
	{
		return MaxSpeed;
	}

	// Blocking is an explicit combat state and takes priority over directional speed.
	// Query the local/predicted GAS-backed component so client prediction and server
	// movement use the state each side owns for this move.
	if (const UPP_CombatComponent* CombatComponent =
		PawnOwner->FindComponentByClass<UPP_CombatComponent>())
	{
		if (CombatComponent->IsBlockingActive())
		{
			return MaxSpeed * BlockingSpeedMultiplier;
		}
	}

	// Acceleration represents voluntary movement input on both the predicting client and
	// server replay. Root motion and external movement therefore keep their normal speed.
	const FVector MovementDirection = Acceleration.GetSafeNormal2D();
	if (MovementDirection.IsNearlyZero())
	{
		return MaxSpeed;
	}

	const FVector FacingDirection = PawnOwner->GetActorForwardVector().GetSafeNormal2D();
	if (FacingDirection.IsNearlyZero())
	{
		return MaxSpeed;
	}

	const float ForwardDot = FVector::DotProduct(FacingDirection, MovementDirection);
	return ForwardDot <= BackwardDotThreshold
		? MaxSpeed * BackwardSpeedMultiplier
		: MaxSpeed;
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
	const FAnimRootMotionCorrectionContext CorrectionContext =
		BuildAnimRootMotionCorrectionContext(this, TimeStamp);
	const bool bCorrectionForDifferentMontage =
		IsCorrectionForDifferentActiveMontage(CorrectionContext);
	const bool bCorrectionTrackAlreadyAligned =
		IsCorrectionTrackAlignedWithSavedMove(CorrectionContext, ServerMontageTrackPosition);
	const bool bSuppressTrackCorrection = bIgnoreServerRootMotionMontageTrackCorrection ||
		bClientIgnoreMovementCorrections || bCorrectionForDifferentMontage ||
		bCorrectionTrackAlreadyAligned;
	const FVector ClientLocationBefore = UpdatedComponent
		? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[ClientRootMotionCorrectionReceived] %s Kind=Montage Timestamp=%.3f ServerLoc=%s ClientLoc=%s LocError=%.2f ServerTrack=%.3f SavedTrack=%.3f SavedMoveFound=%d SavedMontage=%s ActiveMontage=%s DifferentMontage=%d TrackAligned=%d SuppressTrack=%d IgnoreCorrections=%d"),
		*PP_GetNetMotionActorContext(CharacterOwner), TimeStamp, *ServerLoc.ToCompactString(),
		*ClientLocationBefore.ToCompactString(), FVector::Dist(ClientLocationBefore, ServerLoc),
		ServerMontageTrackPosition, CorrectionContext.SavedMoveTrackPosition,
		CorrectionContext.bFoundSavedMove ? 1 : 0, *GetNameSafe(CorrectionContext.SavedMoveMontage.Get()),
		*GetNameSafe(CorrectionContext.ActiveMontage.Get()), bCorrectionForDifferentMontage ? 1 : 0,
		bCorrectionTrackAlreadyAligned ? 1 : 0, bSuppressTrackCorrection ? 1 : 0,
		bClientIgnoreMovementCorrections ? 1 : 0);

	// Base Character Movement first reconciles/acknowledges the saved move, then applies the
	// response's montage position. Suppress only that track write when a local reaction owns the
	// timeline, the ability owns client prediction, the response belongs to a previous montage,
	// or the response already agrees with its saved move.
	FScopedIgnoreMontageTrackCorrection IgnoreMontageTrackCorrection(
		CharacterOwner, bSuppressTrackCorrection);

	// A response produced for a previous montage cannot describe the current ability's capsule.
	// The scoped ignore path acknowledges its saved move without applying that stale location.
	FScopedIgnoreClientMovementCorrection IgnoreStaleMovementCorrection(
		this, bCorrectionForDifferentMontage);
	Super::ClientAdjustRootMotionPosition_Implementation(TimeStamp, ServerMontageTrackPosition, ServerLoc,
		ServerRotation, ServerVelZ, ServerMovementBaseInterfaceData, ServerBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode);
	const FVector ClientLocationAfter = UpdatedComponent
		? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[ClientRootMotionCorrectionApplied] %s Kind=Montage Timestamp=%.3f Before=%s After=%s Moved=%.2f RemainingError=%.2f"),
		*PP_GetNetMotionActorContext(CharacterOwner), TimeStamp,
		*ClientLocationBefore.ToCompactString(), *ClientLocationAfter.ToCompactString(),
		FVector::Dist(ClientLocationBefore, ClientLocationAfter), FVector::Dist(ClientLocationAfter, ServerLoc));
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
	const FAnimRootMotionCorrectionContext CorrectionContext = bHasAnimRootMotion
		? BuildAnimRootMotionCorrectionContext(this, TimeStamp)
		: FAnimRootMotionCorrectionContext();
	const bool bCorrectionForDifferentMontage = bHasAnimRootMotion &&
		IsCorrectionForDifferentActiveMontage(CorrectionContext);
	const bool bCorrectionTrackAlreadyAligned = bHasAnimRootMotion &&
		IsCorrectionTrackAlignedWithSavedMove(CorrectionContext, ServerMontageTrackPosition);
	const bool bSuppressTrackCorrection = bHasAnimRootMotion &&
		(bIgnoreServerRootMotionMontageTrackCorrection || bClientIgnoreMovementCorrections ||
		bCorrectionForDifferentMontage || bCorrectionTrackAlreadyAligned);
	const FVector ClientLocationBefore = UpdatedComponent
		? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[ClientRootMotionCorrectionReceived] %s Kind=Source Timestamp=%.3f HasAnimRootMotion=%d ServerLoc=%s ClientLoc=%s LocError=%.2f ServerTrack=%.3f SavedTrack=%.3f SavedMoveFound=%d SavedMontage=%s ActiveMontage=%s DifferentMontage=%d TrackAligned=%d SuppressTrack=%d IgnoreCorrections=%d"),
		*PP_GetNetMotionActorContext(CharacterOwner), TimeStamp, bHasAnimRootMotion ? 1 : 0,
		*ServerLoc.ToCompactString(), *ClientLocationBefore.ToCompactString(),
		FVector::Dist(ClientLocationBefore, ServerLoc), ServerMontageTrackPosition,
		CorrectionContext.SavedMoveTrackPosition, CorrectionContext.bFoundSavedMove ? 1 : 0,
		*GetNameSafe(CorrectionContext.SavedMoveMontage.Get()),
		*GetNameSafe(CorrectionContext.ActiveMontage.Get()), bCorrectionForDifferentMontage ? 1 : 0,
		bCorrectionTrackAlreadyAligned ? 1 : 0, bSuppressTrackCorrection ? 1 : 0,
		bClientIgnoreMovementCorrections ? 1 : 0);

	// A root-motion-source response can carry the same anim-montage correction. Apply the same
	// timeline and stale-location protections without suppressing root-motion-source reconciliation.
	FScopedIgnoreMontageTrackCorrection IgnoreMontageTrackCorrection(
		CharacterOwner, bSuppressTrackCorrection);
	FScopedIgnoreClientMovementCorrection IgnoreStaleMovementCorrection(
		this, bCorrectionForDifferentMontage);
	Super::ClientAdjustRootMotionSourcePosition_Implementation(TimeStamp, ServerRootMotion, bHasAnimRootMotion,
		ServerMontageTrackPosition, ServerLoc, ServerRotation, ServerVelZ, ServerMovementBaseInterfaceData,
		ServerBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
	const FVector ClientLocationAfter = UpdatedComponent
		? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	UE_CLOG(PP_IsNetMotionDiagnosticEnabled(), LogPPNetMotion, Log,
		TEXT("[ClientRootMotionCorrectionApplied] %s Kind=Source Timestamp=%.3f Before=%s After=%s Moved=%.2f RemainingError=%.2f"),
		*PP_GetNetMotionActorContext(CharacterOwner), TimeStamp,
		*ClientLocationBefore.ToCompactString(), *ClientLocationAfter.ToCompactString(),
		FVector::Dist(ClientLocationBefore, ClientLocationAfter), FVector::Dist(ClientLocationAfter, ServerLoc));
}

