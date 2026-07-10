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

/**
 * Replicates ability movement state and manages the local root-motion collision probe.
 * The owning client publishes state; the server forwards it to simulated proxies.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_AbilityMotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_AbilityMotionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Publishes the current ability movement state to the server and proxies. */
	UFUNCTION(BlueprintCallable, Category="PP Ability Motion")
	void SetAbilityMotionState(const FPP_AbilityMotionState& NewState);

	/** Restores the default movement state and clears temporary overrides. */
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
	/** Latest movement state used locally and replicated to simulated proxies. */
	UPROPERTY(ReplicatedUsing=OnRep_AbilityMotionState)
	FPP_AbilityMotionState AbilityMotionState;

	/** Receives the owning client's predicted movement state on the server. */
	UFUNCTION(Server, Reliable)
	void ServerSetAbilityMotionState(const FPP_AbilityMotionState& NewState);

	/** Enables or restores the requested server movement-correction override. */
	UFUNCTION(Server, Reliable)
	void ServerSetServerMovementCorrectionIgnoreForAbility(bool bEnabled);

	/** Applies replicated movement state to the character movement component. */
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
	// Collision flow: create probe -> track overlaps -> confirm a forward blocker.
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

	/** Temporary capsule used to detect characters in the root-motion path. */
	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> RootMotionCollisionProbeComponent = nullptr;

	/** Character capsules currently overlapping the probe. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ACharacter>> RootMotionCollisionCharacters;

	/** Whether the collision probe is active for the current ability. */
	bool bRootMotionCollisionProbeEnabled = false;
	/** Forward offset of the collision probe. */
	float RootMotionCollisionProbeDistance = 0.f;
	/** Backup search distance used when overlap events arrive late. */
	float RootMotionCollisionFallbackProbeDistance = 0.f;
	/** Maximum forward angle accepted as a blocking character. */
	float RootMotionCollisionForwardAngleDegrees = 0.f;
	/** Last reported blocking state used to avoid duplicate diagnostics. */
	bool bLastLoggedRootMotionCollisionBlocked = false;
	/** Time of the latest blocking result, used by the short grace window. */
	float LastRootMotionCollisionBlockTimeSeconds = -1000.f;

	/** Last correction-ignore value requested from the server. */
	bool bLastRequestedServerMovementCorrectionIgnore = false;
	/** True while the server's original correction setting is saved. */
	bool bHasSavedServerMovementCorrectionIgnore = false;
	/** Original server correction setting restored when the ability ends. */
	bool bSavedServerIgnoreClientMovementErrorChecksAndCorrection = false;
};

