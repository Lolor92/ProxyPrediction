#pragma once

#include "CoreMinimal.h"
#include "AbilityMotion/Types/PP_AnimInstanceTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "PP_AbilityMotionComponent.generated.h"

class AActor;
class ACharacter;
class UCapsuleComponent;
class UPrimitiveComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_AbilityMotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_AbilityMotionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="PP Ability Motion")
	void SetAbilityMotionState(const FPP_AbilityMotionState& NewState);

	UFUNCTION(BlueprintCallable, Category="PP Ability Motion")
	void ResetAbilityMotionState();

	const FPP_AbilityMotionState& GetAbilityMotionState() const { return AbilityMotionState; }

	/** Configures the temporary capsule probe used by locally predicted root-motion abilities. */
	void ConfigureRootMotionCollisionProbe(bool bEnabled, float ProbeDistance, float ForwardAngleDegrees, float FallbackProbeDistance);

	/** Clears probe overlaps and any remembered collision-block state when an ability ends or a new montage starts. */
	void ClearRootMotionCollisionProbe();

	/** Returns true when another character capsule is currently blocking the owner's root-motion path. */
	bool HasRootMotionBlockingCharacterCollision();

	/** Requests server-side movement correction ignore while a prediction-sensitive ability is active. */
	void SetServerMovementCorrectionIgnoreForAbility(bool bEnabled);

protected:
	UPROPERTY(ReplicatedUsing=OnRep_AbilityMotionState)
	FPP_AbilityMotionState AbilityMotionState;

	UFUNCTION(Server, Reliable)
	void ServerSetAbilityMotionState(const FPP_AbilityMotionState& NewState);

	UFUNCTION(Server, Reliable)
	void ServerSetServerMovementCorrectionIgnoreForAbility(bool bEnabled);

	UFUNCTION()
	void OnRep_AbilityMotionState();

	UFUNCTION()
	void OnRootMotionCollisionProbeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRootMotionCollisionProbeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	void ApplyAbilityMotionState(const FPP_AbilityMotionState& NewState);
	ACharacter* GetOwnerCharacter() const;

private:
	void EnsureRootMotionCollisionProbe();
	void ApplyServerMovementCorrectionIgnoreForAbility(bool bEnabled);
	void RebuildRootMotionCollisionOverlaps();
	bool IsRootMotionCollisionCharacterInFront(const ACharacter* OtherCharacter) const;
	bool HasFallbackRootMotionBlockingCharacterCollision(
		float RequiredDot,
		float GraceRequiredDot,
		float& OutAngle,
		float& OutDot,
		ACharacter*& OutCharacter) const;
	void AddRootMotionCollisionCharacter(ACharacter* OtherCharacter);
	void RemoveRootMotionCollisionCharacter(ACharacter* OtherCharacter);

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> RootMotionCollisionProbeComponent = nullptr;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ACharacter>> RootMotionCollisionCharacters;

	bool bRootMotionCollisionProbeEnabled = false;
	float RootMotionCollisionProbeDistance = 0.f;
	float RootMotionCollisionFallbackProbeDistance = 0.f;
	float RootMotionCollisionForwardAngleDegrees = 0.f;
	bool bLastLoggedRootMotionCollisionBlocked = false;
	float LastRootMotionCollisionBlockTimeSeconds = -1000.f;

	bool bLastRequestedServerMovementCorrectionIgnore = false;
	bool bHasSavedServerMovementCorrectionIgnore = false;
	bool bSavedServerIgnoreClientMovementErrorChecksAndCorrection = false;
};

