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

	void ConfigureRootMotionCollisionProbe(bool bEnabled, float ProbeDistance, float ForwardAngleDegrees, float FallbackProbeDistance);
	void ClearRootMotionCollisionProbe();
	bool HasRootMotionBlockingCharacterCollision();

protected:
	UPROPERTY(ReplicatedUsing=OnRep_AbilityMotionState)
	FSyncAbilityMotionState AbilityMotionState;

	UFUNCTION(Server, Reliable)
	void ServerSetAbilityMotionState(const FSyncAbilityMotionState& NewState);

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
};