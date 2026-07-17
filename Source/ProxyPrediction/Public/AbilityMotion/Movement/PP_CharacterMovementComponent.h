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

	/** Protects a locally predicted owner-reaction timeline from server montage-track correction. */
	void SetIgnoreServerRootMotionMontageTrackCorrection(bool bInIgnore);
	bool ShouldIgnoreServerRootMotionMontageTrackCorrection() const
	{
		return bIgnoreServerRootMotionMontageTrackCorrection;
	}

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	/** Reduces voluntary movement speed when acceleration points behind the character. */
	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FVector ScaleInputAcceleration(const FVector& InputAcceleration) const override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Reconciles root-motion moves while protecting locally owned or newer montage timelines from delayed responses. */
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

	/** True while a local owner reaction owns its montage timeline instead of server track position. */
	UPROPERTY(Transient)
	bool bIgnoreServerRootMotionMontageTrackCorrection = false;

	/** True after owner-reaction correction flags have been saved. */
	UPROPERTY(Transient)
	bool bHasSavedOwnerReactionCorrectionFlags = false;

	/** Original client correction-ignore setting restored after the reaction. */
	UPROPERTY(Transient)
	bool bSavedOwnerReactionClientIgnoreMovementCorrections = false;

	/** Original error-check setting restored after the reaction (relevant for a local listen-server owner). */
	UPROPERTY(Transient)
	bool bSavedOwnerReactionIgnoreErrorChecksAndCorrection = false;

protected:
	/** Maximum same-montage rewind treated as a delayed prediction response instead of a new timeline.
	 *  Larger disagreements remain server-authoritative and are corrected normally.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|Network Prediction",
		meta=(ClampMin="0.05", UIMin="0.05", Units="Seconds"))
	float MaxSuppressedRootMotionMontageRewindSeconds = 0.4f;

	/** Maximum historical capsule disagreement for a same-track response to count as an
	 *  acknowledgement of an already-correct saved move rather than a new correction. Fast
	 *  montage movement can span several centimeters between saved-move sampling boundaries;
	 *  this remains well below the distance used to identify a genuinely different path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|Network Prediction",
		meta=(ClampMin="0.0", UIMin="0.0", Units="Centimeters"))
	float AlignedRootMotionCorrectionLocationTolerance = 50.f;

	/** Multiplier applied to normal maximum speed while the character is actively blocking. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|Directional Speed",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float BlockingSpeedMultiplier = 0.6f;

	/** Multiplier applied to normal maximum speed while voluntarily moving backward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|Directional Speed",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float BackwardSpeedMultiplier = 0.6f;

	/** Forward-vector/input dot product at or below which movement counts as backward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|Directional Speed",
		meta=(ClampMin="-1.0", ClampMax="1.0", UIMin="-1.0", UIMax="1.0"))
	float BackwardDotThreshold = -0.5f;
};

