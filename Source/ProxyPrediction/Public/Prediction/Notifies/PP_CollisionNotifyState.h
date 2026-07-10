#pragma once

#include "CoreMinimal.h"
#include "CollisionShape.h"
#include "Prediction/Data/PP_ReactionData.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "PP_CollisionNotifyState.generated.h"

/** Trace shape used by the predicted collision window. */
UENUM(BlueprintType)
enum class EPP_CollisionShape : uint8
{
	Sphere UMETA(ToolTip="Sweep a sphere."),
	Box UMETA(ToolTip="Sweep an oriented box."),
	Capsule UMETA(ToolTip="Sweep an oriented capsule.")
};

/** Per-mesh state that keeps one notify window continuous and duplicate-free. */
struct FPP_NotifyRuntimeWindow
{
	/** Unique identifier for this notify window. */
	FGuid WindowId;

	/** Actors already hit during this window. */
	TSet<TWeakObjectPtr<AActor>> ProcessedTargets;

	/** Whether a previous transform is available for a continuous sweep. */
	bool bHasPreviousSweepTransform = false;
	/** Transform where the preceding sweep ended. */
	FTransform PreviousSweepTransform = FTransform::Identity;
};

/**
 * Sweeps a socket-attached shape during an animation window.
 * A local hit predicts the reaction once, then the server confirms it.
 */
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
	
	/** Draws each predicted sweep for collision debugging. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebug = false;
	
	/** Reaction requested when the sweep finds a new target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predicted Reaction")
	FGameplayTag PredictedReactionTag;

	/** Predicted movement and rotation applied with the reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predicted Reaction|Transform",
		meta=(ShowOnlyInnerProperties))
	FPP_ReactionTransformSettings ReactionTransformSettings;
	
	/** Mesh socket that anchors the collision shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket")
	FName SourceSocketName = TEXT("MainHandWeaponSocket");

	/** Shape swept through the notify window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	EPP_CollisionShape CollisionShape = EPP_CollisionShape::Box;

	/** Radius used by the sphere shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Sphere", EditConditionHides))
	float SphereRadius = 30.f;

	/** Half-size used by the box shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(40.f, 40.f, 10.f);

	/** Radius used by the capsule shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Capsule", EditConditionHides))
	float CapsuleRadius = 20.f;

	/** Half-height used by the capsule shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape",
		meta=(EditCondition="CollisionShape==EPP_CollisionShape::Capsule", EditConditionHides))
	float CapsuleHalfHeight = 45.f;

	/** Shape offset in socket space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	FVector RelativeLocation = FVector::ZeroVector;

	/** Shape rotation offset in socket space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	/** Maximum spacing between sub-sweeps; smaller values reduce tunnelling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision")
	float MaxSweepStepDistance = 35.f;

	/** Collision channel used by the predicted sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;
	
private:
	// Notify flow: validate owner -> build socket transform -> sweep -> predict hit.
	bool ShouldRunPredictedCollision(const AActor* OwnerActor) const;
	bool BuildTraceTransform(USkeletalMeshComponent* MeshComp, FTransform& OutTransform) const;
	void SweepCollision(USkeletalMeshComponent* MeshComp, const FTransform& PreviousTransform,
		const FTransform& CurrentTransform);
	FCollisionShape MakeCollisionShape() const;
	bool HasAlreadyProcessedTarget(USkeletalMeshComponent* MeshComp, AActor* TargetActor) const;
	void MarkTargetProcessed(USkeletalMeshComponent* MeshComp, AActor* TargetActor);
	
	void TryPlayPredictedReaction(AActor* AttackerActor, AActor* HitActor) const;
	
	/** Previous sweep transform retained for active meshes. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<USkeletalMeshComponent>, FTransform> PreviousTransforms;
	
	/** Runtime hit state for each mesh with an active notify window. */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FPP_NotifyRuntimeWindow> ActiveWindowsByMesh;
};

