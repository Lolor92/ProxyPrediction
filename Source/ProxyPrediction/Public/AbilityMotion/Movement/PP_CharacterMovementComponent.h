#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PP_CharacterMovementComponent.generated.h"

UCLASS()
class PROXYPREDICTION_API UPP_CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	void SetAbilityRootMotionSuppressed(bool bInSuppressed);
	bool IsAbilityRootMotionSuppressed() const { return bAbilityRootMotionSuppressed; }
	void RefreshAbilityRootMotionMode();

	void SetAbilityMovementInputSuppressed(bool bInSuppressed);
	bool IsAbilityMovementInputSuppressed() const { return bAbilityMovementInputSuppressed; }

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
	UPROPERTY(Transient)
	bool bAbilityRootMotionSuppressed = false;

	UPROPERTY(Transient)
	bool bAbilityMovementInputSuppressed = false;

	UPROPERTY(Transient)
	bool bIgnoreServerRootMotionMontageTrackCorrection = false;

	UPROPERTY(Transient)
	bool bHasSavedOwnerReactionCorrectionFlags = false;

	UPROPERTY(Transient)
	bool bSavedOwnerReactionClientIgnoreMovementCorrections = false;

	UPROPERTY(Transient)
	bool bSavedOwnerReactionIgnoreErrorChecksAndCorrection = false;

	UPROPERTY(Transient)
	bool bHasOwnerReactionTraceLocation = false;

	UPROPERTY(Transient)
	FVector LastOwnerReactionTraceLocation = FVector::ZeroVector;
};

