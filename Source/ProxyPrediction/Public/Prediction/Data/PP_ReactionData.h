#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PP_ReactionData.generated.h"

class UAnimMontage;
class UGameplayEffect;

/** How a reaction changes forward distance from its reference actor. */
UENUM(BlueprintType)
enum class EPP_ReactionMoveDirection : uint8
{
	None UMETA(DisplayName="None", ToolTip="Do not change location."),
	KeepCurrentDistance UMETA(DisplayName="Keep Current Distance", ToolTip="Keep forward distance and apply only lateral settings."),
	MoveCloser UMETA(DisplayName="Move Closer", ToolTip="Move toward the reference actor."),
	MoveAway UMETA(DisplayName="Move Away", ToolTip="Move away from the reference actor."),
	SnapToDistance UMETA(DisplayName="Snap To Distance", ToolTip="Set an exact distance from the reference actor.")
};

/** How a reaction changes sideways distance from its reference actor. */
UENUM(BlueprintType)
enum class EPP_ReactionLateralOffsetMode : uint8
{
	KeepCurrent UMETA(DisplayName="Keep Current", ToolTip="Keep the current sideways offset."),
	AddOffset UMETA(DisplayName="Add Offset", ToolTip="Add to the current sideways offset."),
	SnapToOffset UMETA(DisplayName="Snap To Offset", ToolTip="Set an exact sideways offset.")
};

/** Actor or actors changed by a reaction transform. */
UENUM(BlueprintType)
enum class EPP_ReactionTransformRecipient : uint8
{
	Instigator UMETA(DisplayName="Instigator", ToolTip="Transform the reaction instigator."),
	Target UMETA(DisplayName="Target", ToolTip="Transform the reaction target."),
	Both UMETA(DisplayName="Both", ToolTip="Transform both actors.")
};

/** Actor that supplies the reference origin and facing axes. */
UENUM(BlueprintType)
enum class EPP_ReactionReferenceActorSource : uint8
{
	Instigator UMETA(DisplayName="Instigator", ToolTip="Use the instigator as reference."),
	Target UMETA(DisplayName="Target", ToolTip="Use the target as reference.")
};

/** Physics handling used when a reaction sets an actor transform. */
UENUM(BlueprintType)
enum class EPP_ReactionTeleportType : uint8
{
	None UMETA(DisplayName="None", ToolTip="Preserve normal movement physics."),
	ResetPhysics UMETA(DisplayName="Reset Physics", ToolTip="Teleport and reset physics state.")
};

/** Direction a reaction recipient should face. */
UENUM(BlueprintType)
enum class EPP_ReactionRotationDirection : uint8
{
	None UMETA(DisplayName="None", ToolTip="Do not change rotation."),
	FaceReferenceActor UMETA(DisplayName="Face Reference Actor", ToolTip="Look toward the reference actor."),
	FaceAwayFromReference UMETA(DisplayName="Face Away From Reference", ToolTip="Look away from the reference actor."),
	FaceOppositeReferenceForward UMETA(DisplayName="Face Opposite Reference Forward", ToolTip="Face opposite the reference actor's forward direction."),
	FaceDirection UMETA(DisplayName="Face Direction", ToolTip="Use the configured world direction.")
};

/** Location adjustment applied when a reaction begins. */
USTRUCT(BlueprintType)
struct FPP_ReactionMovementSettings
{
	GENERATED_BODY()

	/** How forward distance changes relative to the reference actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings")
	EPP_ReactionMoveDirection MoveDirection = EPP_ReactionMoveDirection::None;

	/** Actor or actors whose location is changed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionTransformRecipient Recipient = EPP_ReactionTransformRecipient::Target;

	/** Actor that supplies the origin and forward/right axes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionReferenceActorSource ReferenceActorSource = EPP_ReactionReferenceActorSource::Instigator;

	/** Distance added, removed, or used as the exact snap distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None && MoveDirection != EPP_ReactionMoveDirection::KeepCurrentDistance", EditConditionHides, ClampMin="0.0"))
	float MoveDistance = 25.f;

	/** How sideways offset changes relative to the reference actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionLateralOffsetMode LateralOffsetMode = EPP_ReactionLateralOffsetMode::KeepCurrent;

	/** Sideways distance along the reference actor's right direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None && LateralOffsetMode != EPP_ReactionLateralOffsetMode::KeepCurrent", EditConditionHides))
	float LateralOffset = 0.f;

	/** Sweeps for blocking collision while moving the recipient. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	bool bSweep = true;

	/** Physics handling used when location is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionTeleportType TeleportType = EPP_ReactionTeleportType::ResetPhysics;

	/** Client-only movement speed in cm/s. Zero applies the location instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Settings",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides, ClampMin="0.0"))
	float ClientInterpolationSpeed = 0.0f;
};

/** Rotation adjustment applied when a reaction begins. */
USTRUCT(BlueprintType)
struct FPP_ReactionRotationSettings
{
	GENERATED_BODY()

	/** Direction the recipient should face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings")
	EPP_ReactionRotationDirection RotationDirection = EPP_ReactionRotationDirection::None;

	/** Actor or actors whose rotation is changed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
		meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None", EditConditionHides))
	EPP_ReactionTransformRecipient Recipient = EPP_ReactionTransformRecipient::Target;

	/** Actor that supplies reference position and facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
		meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None", EditConditionHides))
	EPP_ReactionReferenceActorSource ReferenceActorSource = EPP_ReactionReferenceActorSource::Instigator;

	/** World rotation used when Rotation Direction is Face Direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
		meta=(EditCondition="RotationDirection == EPP_ReactionRotationDirection::FaceDirection", EditConditionHides))
	FRotator DirectionToFace = FRotator::ZeroRotator;

	/** Uses a captured facing direction instead of the live reference rotation. */
	UPROPERTY()
	bool bUseReferenceFacingOverride = false;

	/** Captured reference rotation used for confirmed playback. */
	UPROPERTY()
	FRotator ReferenceFacingOverride = FRotator::ZeroRotator;

	/** Physics handling used when rotation is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rotation Settings",
		meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None", EditConditionHides))
	EPP_ReactionTeleportType TeleportType = EPP_ReactionTeleportType::ResetPhysics;
};

/** Complete movement and rotation payload shared by prediction and confirmation. */
USTRUCT(BlueprintType)
struct FPP_ReactionTransformSettings
{
	GENERATED_BODY()

	/** Location changes applied by the reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement", meta=(ShowOnlyInnerProperties))
	FPP_ReactionMovementSettings MovementSettings;

	/** Rotation changes applied by the reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation", meta=(ShowOnlyInnerProperties))
	FPP_ReactionRotationSettings RotationSettings;

	/** Uses the authoritative start transform captured by the server. */
	UPROPERTY()
	bool bUseServerStartTransform = false;

	/** Authoritative reaction start location. */
	UPROPERTY()
	FVector ServerStartLocation = FVector::ZeroVector;

	/** Authoritative reaction start rotation. */
	UPROPERTY()
	FRotator ServerStartRotation = FRotator::ZeroRotator;
};

/** Montage, effects, and timing for one tagged predicted reaction. */
USTRUCT(BlueprintType, meta=(DisplayName="Predicted Reaction"))
struct FPP_ReactionDataEntry
{
	GENERATED_BODY()

public:
	/** Gameplay tag used to request this reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FGameplayTag ReactionTag;

	/** Montage played on the reaction target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/** Playback speed for the reaction montage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	float PlayRate = 1.f;

	/** Optional montage section used as the reaction start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FName StartSection = NAME_None;

	/** Minimum time before the same target may predict this reaction again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	float MinReplayInterval = 0.08f;

	/** Cancels the target's active project ability before a confirmed clean hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	bool bCancelActiveAbilityOnCleanHit = true;

	/** Gameplay effects applied to the target after server confirmation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction", meta=(TitleProperty="GameplayEffectClass"))
	TArray<TSubclassOf<UGameplayEffect>> TargetEffects;
};

/** Data asset that maps reaction tags to predicted reaction definitions. */
UCLASS()
class PROXYPREDICTION_API UPP_ReactionData : public UDataAsset
{
	GENERATED_BODY()
	
public:
	/** Reaction definitions available to the prediction component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncPrediction|Reaction", meta=(TitleProperty="ReactionTag"))
	TArray<FPP_ReactionDataEntry> Reactions;

	/** Finds a reaction or reports an error when the tag is missing. */
	UFUNCTION(BlueprintPure, Category="SyncPrediction|Reaction")
	const FPP_ReactionDataEntry& FindReactionChecked(FGameplayTag ReactionTag) const;

	/** Copies the reaction for a tag and returns whether it was found. */
	UFUNCTION(BlueprintPure, Category="SyncPrediction|Reaction")
	bool FindReaction(FGameplayTag ReactionTag, FPP_ReactionDataEntry& OutReaction) const;

	/** Native lookup that returns null when the tag is not configured. */
	const FPP_ReactionDataEntry* FindReactionPtr(FGameplayTag ReactionTag) const;
};

