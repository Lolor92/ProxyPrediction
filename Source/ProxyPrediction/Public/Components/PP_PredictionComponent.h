#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Data/PP_ReactionData.h"
#include "PP_PredictionComponent.generated.h"

class AActor;

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

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_PredictionComponent : public UActorComponent
{
	GENERATED_BODY()
	
public:
	UPP_PredictionComponent();

	UFUNCTION(BlueprintCallable, Category = "SyncPrediction|Reaction")
	bool PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag);

	UFUNCTION(Server, Reliable)
	void ServerConfirmPredictedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayConfirmedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFinishConfirmedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerFinalLocation);
	
private:
	bool CanPlayPredictedReactionOnTargetProxy(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const;
	FPP_ReactionPredictionContext MakeReactionPredictionContext();
	void AddPendingPredictedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredPendingPredictedReactions();
	
	float GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const;
	
	bool PlayReactionMontageOnActor(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction, float StartPosition,
		bool bForceRestart) const;
	
	bool ConsumePendingPredictedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
	FGameplayTag ReactionTag);

	void AddDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	bool ConsumeDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredDeferredPredictedReactionCorrections();

	void AddPredictedReactionCollisionIgnore(AActor* TargetActor);
	void RemovePredictedReactionCollisionIgnore(AActor* TargetActor);
	void ClearPredictedReactionCollisionIgnores();

	UFUNCTION(Client, Reliable)
	void ClientPlayOwnerConfirmedReaction(FPP_ReactionPredictionContext Context,AActor* TargetActor,
		AActor* InstigatorActor, FGameplayTag ReactionTag);
	
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
	TMap<TWeakObjectPtr<AActor>, int32> PredictedReactionCollisionIgnoreCounts;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float PendingPredictedReactionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float DeferredPredictedCorrectionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float FinalCorrectionTolerance = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction")
	bool bApplyInstantFinalCorrection = true;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxInstantFinalCorrectionDistance = 2.0f;
};
