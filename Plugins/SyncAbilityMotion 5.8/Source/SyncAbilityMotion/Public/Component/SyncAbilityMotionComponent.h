#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/SyncAbilityMotionTypes.h"
#include "Engine/HitResult.h"
#include "SyncAbilityMotionComponent.generated.h"

class ACharacter;
class AActor;
class UCapsuleComponent;
class UPrimitiveComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SYNCABILITYMOTION_API USyncAbilityMotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USyncAbilityMotionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Sync Ability Motion")
	void SetAbilityMotionState(const FSyncAbilityMotionState& NewState);

	UFUNCTION(BlueprintCallable, Category="Sync Ability Motion")
	void ResetAbilityMotionState();

	const FSyncAbilityMotionState& GetAbilityMotionState() const { return AbilityMotionState; }

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
	FSyncAbilityMotionState AbilityMotionState;

	UFUNCTION(Server, Reliable)
	void ServerSetAbilityMotionState(const FSyncAbilityMotionState& NewState);

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

	void ApplyAbilityMotionState(const FSyncAbilityMotionState& NewState);
	ACharacter* GetOwnerCharacter() const;

private:
	void EnsureRootMotionCollisionProbe();
	void ApplyServerMovementCorrectionIgnoreForAbility(bool bEnabled);
	void RebuildRootMotionCollisionOverlaps();
	bool IsRootMotionCollisionCharacterInFront(const ACharacter* OtherCharacter) const;
	/** Performs a short fallback shape check after a confirmed block.
	 *  This protects against overlap loss under latency without widening the normal probe.
	 */
	bool HasFallbackRootMotionBlockingCharacterCollision(
		float RequiredDot,
		float GraceRequiredDot,
		float& OutAngle,
		float& OutDot,
		ACharacter*& OutCharacter) const;
	/** Adds a character capsule to the current root-motion collision cache. */
	void AddRootMotionCollisionCharacter(ACharacter* OtherCharacter);

	/** Removes a character capsule from the current root-motion collision cache. */
	void RemoveRootMotionCollisionCharacter(ACharacter* OtherCharacter);

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> RootMotionCollisionProbeComponent = nullptr;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ACharacter>> RootMotionCollisionCharacters;


	/** True while the owning ability wants character-capsule root-motion blocking. */
	bool bRootMotionCollisionProbeEnabled = false;

	/** Normal overlap probe distance used by the temporary collision capsule. */
	float RootMotionCollisionProbeDistance = 0.f;

	/** Wider fallback distance used briefly after a confirmed block to smooth overlap loss. */
	float RootMotionCollisionFallbackProbeDistance = 0.f;

	/** Forward cone angle used to reject side scrapes. */
	float RootMotionCollisionForwardAngleDegrees = 0.f;

	/** True after the last query found a blocking character. Also enables short release grace checks. */
	bool bLastLoggedRootMotionCollisionBlocked = false;

	/** World time of the last confirmed root-motion character block. */
	float LastRootMotionCollisionBlockTimeSeconds = -1000.f;

	bool bLastRequestedServerMovementCorrectionIgnore = false;
	bool bHasSavedServerMovementCorrectionIgnore = false;
	bool bSavedServerIgnoreClientMovementErrorChecksAndCorrection = false;
};