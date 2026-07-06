#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Data/PP_ReactionData.h"
#include "PP_PredictionComponent.generated.h"

class AActor;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FPP_ReactionPredictionContext
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	bool IsValid() const
	{
		return PredictionId != INDEX_NONE;
	}
};

USTRUCT()
struct FPP_PendingPredictedReaction
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY()
	FGameplayTag ReactionTag;

	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	UPROPERTY()
	double TimeSeconds = 0.0;
};

USTRUCT()
struct FPP_DeferredPredictedReactionCorrection
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY()
	FGameplayTag ReactionTag;

	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	UPROPERTY()
	double TimeSeconds = 0.0;
};

USTRUCT()
struct FPP_OwnerPendingFinalReactionCorrection
{
GENERATED_BODY()

UPROPERTY()
TWeakObjectPtr<AActor> TargetActor;

UPROPERTY()
FGameplayTag ReactionTag;

UPROPERTY()
int32 PredictionId = INDEX_NONE;

UPROPERTY()
FVector ServerFinalLocation = FVector::ZeroVector;

UPROPERTY()
FRotator ServerFinalRotation = FRotator::ZeroRotator;

UPROPERTY()
double TimeSeconds = 0.0;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_PredictionComponent : public UActorComponent
{
	GENERATED_BODY()
	
public:
	UPP_PredictionComponent();

	UFUNCTION(BlueprintCallable, Category = "SyncPrediction|Reaction")
	bool PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag,
		FPP_ReactionTransformSettings TransformSettings);

	UFUNCTION(Server, Reliable)
	void ServerConfirmPredictedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings);
	
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayConfirmedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastFinishConfirmedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerFinalLocation, FRotator ServerFinalRotation);
	
private:
	bool CanPlayPredictedReactionOnTargetProxy(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const;
	FPP_ReactionPredictionContext MakeReactionPredictionContext();
	void AddPendingPredictedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredPendingPredictedReactions();
	
	float GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const;
	void PrepareOwnerReactionRootMotionState(ACharacter* TargetCharacter, const FPP_ReactionPredictionContext& Context,
		FGameplayTag ReactionTag) const;
	
	bool PlayReactionMontageOnActor(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction, float StartPosition,
		bool bForceRestart) const;
	void ApplyTargetEffects(AActor* InstigatorActor, AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const;
	static UAbilitySystemComponent* GetAbilitySystemComponentFromActor(AActor* Actor);

	void ApplyReactionTransform(AActor* InstigatorActor, AActor* TargetActor,
		const FPP_ReactionTransformSettings& TransformSettings) const;
	void ApplyReactionMovement(AActor* RecipientActor, AActor* ReferenceActor,
		const FPP_ReactionMovementSettings& MovementSettings) const;
	void ApplyReactionRotation(AActor* RecipientActor, AActor* ReferenceActor,
		const FPP_ReactionRotationSettings& RotationSettings) const;
	void ApplyReactionTransformToRecipients(AActor* InstigatorActor, AActor* TargetActor, AActor* ReferenceActor,
		EPP_ReactionTransformRecipient Recipient,
		TFunctionRef<void(AActor* RecipientActor, AActor* ReferenceActor)> ApplyFunction) const;
	static AActor* ResolveReactionReferenceActor(AActor* InstigatorActor, AActor* TargetActor,
		EPP_ReactionReferenceActorSource ReferenceActorSource);
	static ETeleportType ToTeleportType(EPP_ReactionTeleportType TeleportType);
	
	bool ConsumePendingPredictedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
	FGameplayTag ReactionTag);

	void AddDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	bool ConsumeDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredDeferredPredictedReactionCorrections();

void AddOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
FGameplayTag ReactionTag, const FVector& ServerFinalLocation, const FRotator& ServerFinalRotation);
bool ConsumeOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
FGameplayTag ReactionTag, FVector& OutServerFinalLocation, FRotator& OutServerFinalRotation);
bool ApplyOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
FGameplayTag ReactionTag, const TCHAR* Reason);
void RemoveExpiredOwnerPendingFinalReactionCorrections();

void AddProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
FGameplayTag ReactionTag, const FVector& ServerFinalLocation, const FRotator& ServerFinalRotation);
bool ConsumeProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
FGameplayTag ReactionTag, FVector& OutServerFinalLocation, FRotator& OutServerFinalRotation);
bool ApplyProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
FGameplayTag ReactionTag, const TCHAR* Reason);
bool ApplyLatestOlderProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, const TCHAR* Reason);
bool HasNewerReactionMarkerForTarget(const FPP_ReactionPredictionContext& Context, AActor* TargetActor);
void RemoveExpiredProxyPendingFinalReactionCorrections();

	UFUNCTION(Client, Reliable)
	void ClientPlayOwnerConfirmedReaction(FPP_ReactionPredictionContext Context,AActor* TargetActor,
		AActor* InstigatorActor, FGameplayTag ReactionTag, FVector ServerStartLocation,
		FRotator ServerStartRotation, FPP_ReactionTransformSettings TransformSettings);
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SyncPrediction|Reaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPP_ReactionData> ReactionData = nullptr;
	
	UPROPERTY(Transient)
	mutable TMap<TWeakObjectPtr<AActor>, double> LastReactionTimeByTarget;
	
	UPROPERTY(Transient)
	int32 NextPredictionId = 0;
	
	static constexpr int32 MaxPredictionId = 32767;
	static constexpr int32 MaxPendingPredictedReactions = 32;
	
	UPROPERTY(Transient)
	TArray<FPP_PendingPredictedReaction> PendingPredictedReactions;

	UPROPERTY(Transient)
	TArray<FPP_DeferredPredictedReactionCorrection> DeferredPredictedReactionCorrections;

UPROPERTY(Transient)
TArray<FPP_OwnerPendingFinalReactionCorrection> OwnerPendingFinalReactionCorrections;

UPROPERTY(Transient)
TArray<FPP_OwnerPendingFinalReactionCorrection> ProxyPendingFinalReactionCorrections;

UPROPERTY(Transient)
int32 OwnerReactionCorrectionSuppressionCount = 0;

	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AActor>, int32> PredictedReactionCollisionIgnoreCounts;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float PendingPredictedReactionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float DeferredPredictedCorrectionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float FinalCorrectionTolerance = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Degrees"))
	float FinalRotationCorrectionTolerance = 1.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction")
	bool bApplyInstantFinalCorrection = true;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxInstantFinalCorrectionDistance = 500.0f;

UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", ClampMax="0.25", Units="Seconds"))
float ProxyFinalCorrectionDelaySeconds = 0.05f;

UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", ClampMax="0.5", Units="Seconds"))
float OwnerFinalCorrectionSmoothSeconds = 0.15f;
};
