#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PP_CharacterMovementComponent.generated.h"

/** Character movement support for predicted ability root motion and input locks. */
UCLASS()
class PROXYPREDICTION_API UPP_CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** Toggles whether montage root motion may move the character. */
	void SetAbilityRootMotionSuppressed(bool bInSuppressed);
	bool IsAbilityRootMotionSuppressed() const { return bAbilityRootMotionSuppressed; }
	/** Reapplies the correct root-motion mode after suppression changes. */
	void RefreshAbilityRootMotionMode();

	/** Toggles predicted movement-input suppression for the active ability. */
	void SetAbilityMovementInputSuppressed(bool bInSuppressed);
	bool IsAbilityMovementInputSuppressed() const { return bAbilityMovementInputSuppressed; }

	/** Separately suppresses voluntary movement while the character owns a CC tag. */
	void SetCrowdControlMovementInputSuppressed(bool bInSuppressed);
	bool IsCrowdControlMovementInputSuppressed() const { return bCrowdControlMovementInputSuppressed; }

	/** Ignores montage track correction during visual-only owner reaction playback. */
	void SetIgnoreServerRootMotionMontageTrackCorrection(bool bInIgnore);
	bool ShouldIgnoreServerRootMotionMontageTrackCorrection() const
	{
		return bIgnoreServerRootMotionMontageTrackCorrection;
	}

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FVector ScaleInputAcceleration(const FVector& InputAcceleration) const override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	// Keep capsule correction while optionally ignoring montage-track correction.
	virtual void ClientAdjustRootMotionPosition_Implementation(float TimeStamp, float ServerMontageTrackPosition,
		FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase,
		FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientAdjustRootMotionPosition_Implementation(float TimeStamp, float ServerMontageTrackPosition,
		FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ,
		FMovementBaseInterfaceData* ServerMovementBaseInterfaceData, FName ServerBoneName, bool bHasBase,
		bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientAdjustRootMotionSourcePosition_Implementation(float TimeStamp, FRootMotionSourceGroup ServerRootMotion,
		bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc,
		FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase,
		FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientAdjustRootMotionSourcePosition_Implementation(float TimeStamp, FRootMotionSourceGroup ServerRootMotion,
		bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc,
		FVector_NetQuantizeNormal ServerRotation, float ServerVelZ,
		FMovementBaseInterfaceData* ServerMovementBaseInterfaceData, FName ServerBoneName, bool bHasBase,
		bool bBaseRelativePosition, uint8 ServerMovementMode) override;

private:
	/** True while ability montage root motion is disabled. */
	UPROPERTY(Transient)
	bool bAbilityRootMotionSuppressed = false;

	/** True while ability input should produce no movement acceleration. */
	UPROPERTY(Transient)
	bool bAbilityMovementInputSuppressed = false;

	/** True while authoritative or replicated crowd control blocks movement input. */
	UPROPERTY(Transient)
	bool bCrowdControlMovementInputSuppressed = false;

	/** True while owner reaction playback ignores server montage position. */
	UPROPERTY(Transient)
	bool bIgnoreServerRootMotionMontageTrackCorrection = false;

	/** True after owner-reaction correction flags have been saved. */
	UPROPERTY(Transient)
	bool bHasSavedOwnerReactionCorrectionFlags = false;

	/** Original client movement-correction setting restored after the reaction. */
	UPROPERTY(Transient)
	bool bSavedOwnerReactionClientIgnoreMovementCorrections = false;

	/** Original server error-correction setting restored after the reaction. */
	UPROPERTY(Transient)
	bool bSavedOwnerReactionIgnoreErrorChecksAndCorrection = false;

};

