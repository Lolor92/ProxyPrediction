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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Block Settings")
	bool bBlockable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Block Settings",
		meta=(EditCondition="bBlockable", EditConditionHides, ClampMin="0.0", ClampMax="180.0", Units="Degrees"))
	float BlockAngleDegrees = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Block Settings",
		meta=(EditCondition="bBlockable", EditConditionHides))
	bool bAllowMovementWhenBlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Block Settings",
		meta=(EditCondition="bBlockable", EditConditionHides))
	bool bAllowRotationWhenBlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dodge Settings")
	bool bDodgeable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Super Armor Settings")
	EPP_SuperArmorLevel RequiredSuperArmor = EPP_SuperArmorLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage Settings", meta=(TitleProperty="GameplayEffectClass"))
	TArray<FPP_ReactionDamageEffect> DamageEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage Settings")
	bool bApplyDamageWhenBlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage Settings")
	bool bApplyDamageWhenParried = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage Settings")
	bool bApplyDamageWhenDodged = false;

	/** How forward distance changes relative to the reference actor. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings")
EPP_ReactionMoveDirection MoveDirection = EPP_ReactionMoveDirection::None;

/** Actor or actors whose location is changed. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
EPP_ReactionTransformRecipient MovementRecipient =
EPP_ReactionTransformRecipient::Target;

/** Actor that supplies the movement origin and axes. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
EPP_ReactionReferenceActorSource MovementReferenceActorSource =
EPP_ReactionReferenceActorSource::Instigator;

/** Distance added, removed, or used as the exact snap distance. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None && MoveDirection != EPP_ReactionMoveDirection::KeepCurrentDistance",
EditConditionHides, ClampMin="0.0"))
float MoveDistance = 25.0f;

/** How sideways offset changes relative to the reference actor. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
EPP_ReactionLateralOffsetMode LateralOffsetMode =
EPP_ReactionLateralOffsetMode::KeepCurrent;

/** Sideways distance along the reference actor right axis. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None && LateralOffsetMode != EPP_ReactionLateralOffsetMode::KeepCurrent",
EditConditionHides))
float LateralOffset = 0.0f;

/** Sweeps while moving the recipient. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
bool bSweepMovement = true;

/** Physics handling used when movement is applied. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
EPP_ReactionTeleportType MovementTeleportType =
EPP_ReactionTeleportType::None;

/** Client-only interpolation speed in cm/s. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None",
EditConditionHides, ClampMin="0.0"))
float ClientInterpolationSpeed = 0.0f;

/** Direction the recipient should face. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings")
EPP_ReactionRotationDirection RotationDirection =
EPP_ReactionRotationDirection::None;

/** Actor or actors whose rotation is changed. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None",
EditConditionHides))
EPP_ReactionTransformRecipient RotationRecipient =
EPP_ReactionTransformRecipient::Target;

/** Actor that supplies the rotation reference. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None",
EditConditionHides))
EPP_ReactionReferenceActorSource RotationReferenceActorSource =
EPP_ReactionReferenceActorSource::Instigator;

/** World rotation used by Face Direction. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
meta=(EditCondition="RotationDirection == EPP_ReactionRotationDirection::FaceDirection",
EditConditionHides))
FRotator DirectionToFace = FRotator::ZeroRotator;

/** Physics handling used when rotation is applied. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None",
EditConditionHides))
EPP_ReactionTeleportType RotationTeleportType =
EPP_ReactionTeleportType::None;

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

