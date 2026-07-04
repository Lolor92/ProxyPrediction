#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PP_ReactionData.generated.h"

class UAnimMontage;
class UGameplayEffect;

UENUM(BlueprintType)
enum class EPP_ReactionMoveDirection : uint8
{
	None UMETA(DisplayName="None"),
	KeepCurrentDistance UMETA(DisplayName="Keep Current Distance"),
	MoveCloser UMETA(DisplayName="Move Closer"),
	MoveAway UMETA(DisplayName="Move Away"),
	SnapToDistance UMETA(DisplayName="Snap To Distance")
};

UENUM(BlueprintType)
enum class EPP_ReactionLateralOffsetMode : uint8
{
	KeepCurrent UMETA(DisplayName="Keep Current"),
	AddOffset UMETA(DisplayName="Add Offset"),
	SnapToOffset UMETA(DisplayName="Snap To Offset")
};

UENUM(BlueprintType)
enum class EPP_ReactionTransformRecipient : uint8
{
	Instigator UMETA(DisplayName="Instigator"),
	Target UMETA(DisplayName="Target"),
	Both UMETA(DisplayName="Both")
};

UENUM(BlueprintType)
enum class EPP_ReactionReferenceActorSource : uint8
{
	Instigator UMETA(DisplayName="Instigator"),
	Target UMETA(DisplayName="Target")
};

UENUM(BlueprintType)
enum class EPP_ReactionTeleportType : uint8
{
	None UMETA(DisplayName="None"),
	ResetPhysics UMETA(DisplayName="Reset Physics")
};

UENUM(BlueprintType)
enum class EPP_ReactionRotationDirection : uint8
{
	None UMETA(DisplayName="None"),
	FaceReferenceActor UMETA(DisplayName="Face Reference Actor"),
	FaceAwayFromReference UMETA(DisplayName="Face Away From Reference"),
	FaceOppositeReferenceForward UMETA(DisplayName="Face Opposite Reference Forward"),
	FaceDirection UMETA(DisplayName="Face Direction")
};

USTRUCT(BlueprintType)
struct FPP_ReactionMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement")
	EPP_ReactionMoveDirection MoveDirection = EPP_ReactionMoveDirection::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionTransformRecipient Recipient = EPP_ReactionTransformRecipient::Target;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionReferenceActorSource ReferenceActorSource = EPP_ReactionReferenceActorSource::Instigator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None && MoveDirection != EPP_ReactionMoveDirection::KeepCurrentDistance", EditConditionHides, ClampMin="0.0"))
	float MoveDistance = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionLateralOffsetMode LateralOffsetMode = EPP_ReactionLateralOffsetMode::KeepCurrent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None && LateralOffsetMode != EPP_ReactionLateralOffsetMode::KeepCurrent", EditConditionHides))
	float LateralOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	bool bSweep = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement",
		meta=(EditCondition="MoveDirection != EPP_ReactionMoveDirection::None", EditConditionHides))
	EPP_ReactionTeleportType TeleportType = EPP_ReactionTeleportType::ResetPhysics;
};

USTRUCT(BlueprintType)
struct FPP_ReactionRotationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation")
	EPP_ReactionRotationDirection RotationDirection = EPP_ReactionRotationDirection::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation",
		meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None", EditConditionHides))
	EPP_ReactionTransformRecipient Recipient = EPP_ReactionTransformRecipient::Target;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation",
		meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None", EditConditionHides))
	EPP_ReactionReferenceActorSource ReferenceActorSource = EPP_ReactionReferenceActorSource::Instigator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation",
		meta=(EditCondition="RotationDirection == EPP_ReactionRotationDirection::FaceDirection", EditConditionHides))
	FRotator DirectionToFace = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation",
		meta=(EditCondition="RotationDirection != EPP_ReactionRotationDirection::None", EditConditionHides))
	EPP_ReactionTeleportType TeleportType = EPP_ReactionTeleportType::ResetPhysics;
};

USTRUCT(BlueprintType)
struct FPP_ReactionTransformSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Movement", meta=(ShowOnlyInnerProperties))
	FPP_ReactionMovementSettings MovementSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction|Rotation", meta=(ShowOnlyInnerProperties))
	FPP_ReactionRotationSettings RotationSettings;
};

USTRUCT(BlueprintType, meta=(DisplayName="Predicted Reaction"))
struct FPP_ReactionDataEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FGameplayTag ReactionTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FName StartSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	float MinReplayInterval = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	bool bCancelActiveAbilityOnCleanHit = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction", meta=(TitleProperty="GameplayEffectClass"))
	TArray<TSubclassOf<UGameplayEffect>> TargetEffects;
};

UCLASS()
class PROXYPREDICTION_API UPP_ReactionData : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncPrediction|Reaction", meta=(TitleProperty="ReactionTag"))
	TArray<FPP_ReactionDataEntry> Reactions;

	UFUNCTION(BlueprintPure, Category="SyncPrediction|Reaction")
	const FPP_ReactionDataEntry& FindReactionChecked(FGameplayTag ReactionTag) const;

	UFUNCTION(BlueprintPure, Category="SyncPrediction|Reaction")
	bool FindReaction(FGameplayTag ReactionTag, FPP_ReactionDataEntry& OutReaction) const;

	const FPP_ReactionDataEntry* FindReactionPtr(FGameplayTag ReactionTag) const;
};
