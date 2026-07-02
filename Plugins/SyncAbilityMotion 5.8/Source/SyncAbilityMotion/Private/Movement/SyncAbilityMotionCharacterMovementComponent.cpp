#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"

#include "AnimInstance/SyncAbilityMotionAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

namespace SyncAbilityMotionFlags
{
	constexpr uint8 SuppressAbilityRootMotion = FSavedMove_Character::FLAG_Custom_0;
	constexpr uint8 SuppressAbilityMovementInput = FSavedMove_Character::FLAG_Custom_1;
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

	const bool bRootMotionEnabled = !bAbilityRootMotionSuppressed;
	AnimInstance->bRootMotionEnabled = bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(bRootMotionEnabled
		? ERootMotionMode::RootMotionFromMontagesOnly
		: ERootMotionMode::IgnoreRootMotion);
}

void USyncAbilityMotionCharacterMovementComponent::SetAbilityMovementInputSuppressed(bool bInSuppressed)
{
	bAbilityMovementInputSuppressed = bInSuppressed;
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

FVector USyncAbilityMotionCharacterMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration) const
{
	if (bAbilityMovementInputSuppressed)
	{
		return FVector::ZeroVector;
	}

	return Super::ScaleInputAcceleration(InputAcceleration);
}
