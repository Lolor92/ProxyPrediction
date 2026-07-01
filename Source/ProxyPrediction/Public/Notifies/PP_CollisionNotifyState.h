#pragma once

#include "CoreMinimal.h"
#include "CollisionShape.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "PP_CollisionNotifyState.generated.h"

UENUM(BlueprintType)
enum class EPP_CollisionShape : uint8
{
	Sphere,
	Box,
	Capsule
};

struct FPP_NotifyRuntimeWindow
{
	FGuid WindowId;

	TSet<TWeakObjectPtr<AActor>> ProcessedTargets;

	bool bHasPreviousSweepTransform = false;
	FTransform PreviousSweepTransform = FTransform::Identity;
};

UCLASS()
class PROXYPREDICTION_API UPP_CollisionNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebug = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predicted Reaction")
	FGameplayTag PredictedReactionTag;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket")
	FName SourceSocketName = TEXT("MainHandWeaponSocket");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	EPP_CollisionShape CollisionShape = EPP_CollisionShape::Box;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Sphere", EditConditionHides))
	float SphereRadius = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(40.f, 40.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Capsule", EditConditionHides))
	float CapsuleRadius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Capsule", EditConditionHides))
	float CapsuleHalfHeight = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision")
	float MaxSweepStepDistance = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;
	
private:
	bool ShouldRunPredictedCollision(const AActor* OwnerActor) const;
	bool BuildTraceTransform(USkeletalMeshComponent* MeshComp, FTransform& OutTransform) const;
	void SweepCollision(USkeletalMeshComponent* MeshComp, const FTransform& PreviousTransform,
		const FTransform& CurrentTransform);
	FCollisionShape MakeCollisionShape() const;
	bool HasAlreadyProcessedTarget(USkeletalMeshComponent* MeshComp, AActor* TargetActor) const;
	void MarkTargetProcessed(USkeletalMeshComponent* MeshComp, AActor* TargetActor);
	
	void TryPlayPredictedReaction(AActor* AttackerActor, AActor* HitActor) const;
	
	UPROPERTY(Transient)
	TMap<TObjectPtr<USkeletalMeshComponent>, FTransform> PreviousTransforms;
	
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FPP_NotifyRuntimeWindow> ActiveWindowsByMesh;
};
