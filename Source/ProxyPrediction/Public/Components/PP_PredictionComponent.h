#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Data/PP_ReactionData.h"
#include "PP_PredictionComponent.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FPMMO_ReactionPredictionContext
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
struct FPMMO_PendingPredictedReaction
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
struct FPMMO_DeferredPredictedReactionCorrection
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

UCLASS()
class PROXYPREDICTION_API UPP_PredictionComponent : public UActorComponent
{
	GENERATED_BODY()
	
public:
	UPP_PredictionComponent();

	UFUNCTION(BlueprintCallable, Category = "SyncPrediction|Reaction")
	bool PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag);

	UFUNCTION(Server, Reliable)
	void ServerConfirmPredictedReaction(FPMMO_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayConfirmedReaction(FPMMO_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFinishConfirmedReaction(FPMMO_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerFinalLocation);
	
private:
	bool CanPlayPredictedReactionOnTargetProxy(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const;
	FPMMO_ReactionPredictionContext MakeReactionPredictionContext();
	void AddPendingPredictedReaction(const FPMMO_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredPendingPredictedReactions();
	
	float GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const;
	
	bool PlayReactionMontageOnActor(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction, float StartPosition,
		bool bForceRestart) const;
	
	bool ConsumePendingPredictedReaction(const FPMMO_ReactionPredictionContext& Context, AActor* TargetActor,
	FGameplayTag ReactionTag);

	void AddDeferredPredictedReactionCorrection(const FPMMO_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	bool ConsumeDeferredPredictedReactionCorrection(const FPMMO_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredDeferredPredictedReactionCorrections();

	UFUNCTION(Client, Reliable)
	void ClientPlayOwnerConfirmedReaction(FPMMO_ReactionPredictionContext Context,AActor* TargetActor,
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
	TArray<FPMMO_PendingPredictedReaction> PendingPredictedReactions;

	UPROPERTY(Transient)
	TArray<FPMMO_DeferredPredictedReactionCorrection> DeferredPredictedReactionCorrections;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float PendingPredictedReactionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float DeferredPredictedCorrectionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float FinalCorrectionTolerance = 2.0f;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction")
	bool bApplyInstantFinalCorrection = false;

	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxInstantFinalCorrectionDistance = 35.0f;
};
