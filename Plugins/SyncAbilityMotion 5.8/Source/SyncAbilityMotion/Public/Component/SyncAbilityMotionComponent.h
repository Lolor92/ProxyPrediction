#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/SyncAbilityMotionTypes.h"
#include "SyncAbilityMotionComponent.generated.h"

class ACharacter;

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

protected:
	UPROPERTY(ReplicatedUsing=OnRep_AbilityMotionState)
	FSyncAbilityMotionState AbilityMotionState;

	UFUNCTION(Server, Reliable)
	void ServerSetAbilityMotionState(const FSyncAbilityMotionState& NewState);

	UFUNCTION()
	void OnRep_AbilityMotionState();

	void ApplyAbilityMotionState(const FSyncAbilityMotionState& NewState);
	ACharacter* GetOwnerCharacter() const;
};
