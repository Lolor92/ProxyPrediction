#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Prediction/Data/PP_ReactionData.h"
#include "TimerManager.h"
#include "PP_PredictionComponent.generated.h"

class AActor;
class ACharacter;
class UAbilitySystemComponent;
class UAnimMontage;
class UCharacterMovementComponent;
class USkeletalMeshComponent;

/** Identifier carried through the predicted and confirmed reaction flow. */
USTRUCT(BlueprintType)
struct FPP_ReactionPredictionContext
{
	GENERATED_BODY()

	/** Client-generated identifier used to match prediction with confirmation. */
	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	bool IsValid() const
	{
		return PredictionId != INDEX_NONE;
	}
};

/** Local prediction waiting for its server start confirmation. */
USTRUCT()
struct FPP_PendingPredictedReaction
{
	GENERATED_BODY()

	/** Proxy that received the predicted reaction. */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	/** Reaction used to match the confirmation. */
	UPROPERTY()
	FGameplayTag ReactionTag;

	/** Prediction identifier used to match the confirmation. */
	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	/** Local creation time used to expire stale predictions. */
	UPROPERTY()
	double TimeSeconds = 0.0;

	/** Whether this client actually played the clean reaction montage. */
	UPROPERTY()
	bool bPlayedLocally = false;
};

/** Confirmed prediction waiting for its authoritative final transform. */
USTRUCT()
struct FPP_DeferredPredictedReactionCorrection
{
	GENERATED_BODY()

	/** Proxy waiting for final correction. */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	/** Reaction used to match the final correction. */
	UPROPERTY()
	FGameplayTag ReactionTag;

	/** Prediction identifier used to match the final correction. */
	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	/** Local creation time used to expire stale markers. */
	UPROPERTY()
	double TimeSeconds = 0.0;
};

/** Authoritative final transform queued for an owner or predicted proxy. */
USTRUCT()
struct FPP_OwnerPendingFinalReactionCorrection
{
	GENERATED_BODY()

	/** Actor that should receive the final correction. */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	/** Reaction used to match the queued correction. */
	UPROPERTY()
	FGameplayTag ReactionTag;

	/** Prediction identifier used to match the queued correction. */
	UPROPERTY()
	int32 PredictionId = INDEX_NONE;

	/** Authoritative location at the end of the reaction. */
	UPROPERTY()
	FVector ServerFinalLocation = FVector::ZeroVector;

	/** Authoritative rotation at the end of the reaction. */
	UPROPERTY()
	FRotator ServerFinalRotation = FRotator::ZeroRotator;

	/** Local creation time used to expire stale corrections. */
	UPROPERTY()
	double TimeSeconds = 0.0;
};

/** Restores a simulated proxy after client-only mesh interpolation finishes. */
struct FPP_ProxyReactionVisualInterpolation
{
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
	FVector RestingRelativeLocation = FVector::ZeroVector;
	uint8 SavedNetworkSmoothingMode = 0;
};

/** Server-observed collision waiting to be matched with the predicting client's request. */
USTRUCT()
struct FPP_AuthoritativeCollisionRecord
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY()
	FGameplayTag ReactionTag;

	UPROPERTY()
	FPP_ReactionTransformSettings TransformSettings;

	UPROPERTY()
	FPP_ReactionDefenseSettings DefenseSettings;

	UPROPERTY()
	FPP_ReactionDamageSettings DamageSettings;

	UPROPERTY()
	double TimeSeconds = 0.0;
};

/** Client reaction request waiting for the server notify sweep to observe the same target. */
USTRUCT()
struct FPP_PendingServerReactionRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FPP_ReactionPredictionContext Context;

	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY()
	FGameplayTag ReactionTag;

	UPROPERTY()
	double TimeSeconds = 0.0;
};

/** Collision-notify match waiting for a frame where it can start before target Character Movement. */
struct FPP_DeferredServerReactionFromCollision
{
	FPP_ReactionPredictionContext Context;
	TWeakObjectPtr<AActor> TargetActor;
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
	FGameplayTag ReactionTag;
	FPP_ReactionTransformSettings TransformSettings;
	FPP_ReactionDefenseSettings DefenseSettings;
	FPP_ReactionDamageSettings DamageSettings;
	uint64 EarliestFrameCounter = 0;
};

/** Short post-reaction window that lets normal owner movement replication settle before final reconciliation. */
struct FPP_OwnerFinalReconciliationGraceState
{
	TWeakObjectPtr<AActor> TargetActor;
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
	FGameplayTag ReactionTag;
	int32 PredictionId = INDEX_NONE;
	bool bHoldMovementCorrections = false;
};

/** Guards a confirmed locally predicted proxy from server root-motion replay until its montage ends. */
struct FPP_PredictedProxyRootMotionReconciliation
{
	TWeakObjectPtr<ACharacter> TargetCharacter;
	TWeakObjectPtr<UAnimMontage> Montage;
	double ExpireTimeSeconds = 0.0;
};

/**
 * Predicts target reactions immediately, then reconciles them with the server.
 * Prediction markers prevent duplicate playback and route the final correction.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_PredictionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_PredictionComponent();

	/** Predicts the target transform and montage, then asks the server to confirm them. */
	UFUNCTION(BlueprintCallable, Category="SyncPrediction|Reaction")
	bool PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag,
		FPP_ReactionTransformSettings TransformSettings, FPP_ReactionDefenseSettings DefenseSettings,
		FPP_ReactionDamageSettings DamageSettings);

	/** Records a server notify hit and resolves a matching client request using server-authored settings. */
	void RecordAuthoritativeCollision(AActor* TargetActor, FGameplayTag ReactionTag,
		const FPP_ReactionTransformSettings& TransformSettings,
		const FPP_ReactionDefenseSettings& DefenseSettings,
		const FPP_ReactionDamageSettings& DamageSettings);

	/** Confirms a reaction using transform settings stored in server Reaction Data. */
	UFUNCTION(Server, Reliable)
	void ServerConfirmPredictedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings,
		FPP_ReactionDefenseSettings DefenseSettings, FPP_ReactionDamageSettings DamageSettings);

	/** Override for server-only ability, team, line-of-sight, or combat-rule validation. */
	UFUNCTION(BlueprintNativeEvent, Category="SyncPrediction|Server Validation")
	bool CanServerAcceptPredictedReaction(AActor* TargetActor, FGameplayTag ReactionTag,
		const FPP_ReactionTransformSettings& TransformSettings) const;

	/** Starts cosmetic proxy playback without replaying a matching local prediction. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayConfirmedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerStartLocation, FRotator ServerStartRotation,
		float ClientInterpolationSpeed, bool bPlayReaction);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Server validation rejects spam, distant targets, and unsafe transform requests.
	bool ConsumeServerReactionRequestBudget();
	bool ValidateServerReactionRequest(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction,
		const FPP_ReactionTransformSettings& TransformSettings);
	bool ProcessServerConfirmedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag, const FPP_ReactionTransformSettings& TransformSettings,
		const FPP_ReactionDefenseSettings& DefenseSettings,
		const FPP_ReactionDamageSettings& DamageSettings);
	void DeferServerConfirmedReactionFromCollision(const FPP_ReactionPredictionContext& Context,
		AActor* TargetActor, FGameplayTag ReactionTag,
		const FPP_ReactionTransformSettings& TransformSettings,
		const FPP_ReactionDefenseSettings& DefenseSettings,
		const FPP_ReactionDamageSettings& DamageSettings);
	void ProcessDeferredServerReactionsFromCollision();
	void ScheduleServerRootMotionStartAlignment(const FPP_ReactionPredictionContext& Context,
		AActor* TargetActor, FGameplayTag ReactionTag, UAnimMontage* Montage,
		float StartPosition, const FVector& ServerStartLocation, int32 Attempt = 0);
	void RemoveExpiredAuthoritativeCollisionState();
	bool ConsumeAuthoritativeCollision(AActor* TargetActor, FGameplayTag ReactionTag,
		FPP_ReactionTransformSettings& OutTransformSettings,
		FPP_ReactionDefenseSettings& OutDefenseSettings,
		FPP_ReactionDamageSettings& OutDamageSettings);
	void QueuePendingServerReactionRequest(const FPP_ReactionPredictionContext& Context,
		AActor* TargetActor, FGameplayTag ReactionTag);
	EPP_ReactionDefenseOutcome ResolveDefenseOutcome(AActor* TargetActor,
		const FPP_ReactionDefenseSettings& DefenseSettings) const;
	static bool ShouldApplyReactionTransform(EPP_ReactionDefenseOutcome Outcome,
		const FPP_ReactionDefenseSettings& DefenseSettings);
	void ApplyDefenseOutcomeEffects(AActor* TargetActor, EPP_ReactionDefenseOutcome Outcome) const;
	bool ShouldApplyDamage(AActor* TargetActor, EPP_ReactionDefenseOutcome Outcome,
		const FPP_ReactionDamageSettings& DamageSettings) const;
	void ApplyDamageEffects(AActor* TargetActor, const FPP_ReactionDamageSettings& DamageSettings) const;

	// Prediction start: validate -> create marker -> transform -> montage -> server RPC.
	bool CanPlayPredictedReactionOnTargetProxy(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const;
	FPP_ReactionPredictionContext MakeReactionPredictionContext();
	void AddPendingPredictedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag, bool bPlayedLocally);
	void RemoveExpiredPendingPredictedReactions();

	float GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const;
	void PrepareOwnerReactionRootMotionState(ACharacter* TargetCharacter, const FPP_ReactionPredictionContext& Context,
		FGameplayTag ReactionTag) const;

	// Shared montage, effect, and GAS helpers.
	bool PlayReactionMontageOnActor(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction, float StartPosition,
		bool bForceRestart) const;
	void ApplyTargetEffects(AActor* InstigatorActor, AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const;
	static UAbilitySystemComponent* GetAbilitySystemComponentFromActor(AActor* Actor);

	// Transform flow: resolve reference -> select recipients -> apply rotation and movement.
	void ApplyReactionTransform(AActor* InstigatorActor, AActor* TargetActor,
		const FPP_ReactionTransformSettings& TransformSettings);
	void ApplyReactionMovement(AActor* RecipientActor, AActor* ReferenceActor,
		const FPP_ReactionMovementSettings& MovementSettings);
	void ApplyReactionRotation(AActor* RecipientActor, AActor* ReferenceActor,
		const FPP_ReactionRotationSettings& RotationSettings) const;
	void ApplyReactionTransformToRecipients(AActor* InstigatorActor, AActor* TargetActor, AActor* ReferenceActor,
		EPP_ReactionTransformRecipient Recipient,
		TFunctionRef<void(AActor* RecipientActor, AActor* ReferenceActor)> ApplyFunction);
	void SetReactionActorLocation(AActor* RecipientActor, const FVector& TargetLocation,
		const FPP_ReactionMovementSettings& MovementSettings, bool bForceNoSweep = false);
	void StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor> RecipientActor);
	void StopAllClientReactionMovementInterpolations();
	static AActor* ResolveReactionReferenceActor(AActor* InstigatorActor, AActor* TargetActor,
		EPP_ReactionReferenceActorSource ReferenceActorSource);
	static ETeleportType ToTeleportType(EPP_ReactionTeleportType TeleportType);

	// Start-confirmation markers suppress duplicate montage playback.
	bool ConsumePendingPredictedReaction(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag* OutPredictedReactionTag = nullptr, bool* bOutPlayedLocally = nullptr);
	void AddDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	bool ConsumeDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag);
	void RemoveExpiredDeferredPredictedReactionCorrections();

	// Locally controlled targets smooth to their authoritative final transform.
	void AddOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag, const FVector& ServerFinalLocation, const FRotator& ServerFinalRotation);
	bool ConsumeOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector& OutServerFinalLocation, FRotator& OutServerFinalRotation);
	bool PeekOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector& OutServerFinalLocation, FRotator& OutServerFinalRotation);
	bool ApplyOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context, AActor* TargetActor,
		FGameplayTag ReactionTag, const TCHAR* Reason);
	void RemoveExpiredOwnerPendingFinalReactionCorrections();
	bool BeginOwnerFinalReconciliationGrace(const FPP_ReactionPredictionContext& Context,
		AActor* TargetActor, FGameplayTag ReactionTag);
	bool IsOwnerFinalReconciliationGraceActive(const FPP_ReactionPredictionContext& Context,
		AActor* TargetActor, FGameplayTag ReactionTag) const;

	// Predicting proxies apply only the final correction matching their marker.
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
	void BeginPredictedProxyRootMotionReconciliation(const FPP_ReactionPredictionContext& Context,
		AActor* TargetActor, FGameplayTag ReactionTag);
	void UpdatePredictedProxyRootMotionReconciliations();
	void RemovePredictedProxyRootMotionReconciliation(int32 Index);
	/** Plays owner feedback and starts the local correction-suppression window. */
	UFUNCTION(Client, Reliable)
	void ClientPlayOwnerConfirmedReaction(FPP_ReactionPredictionContext Context, FGameplayTag ReactionTag,
		FVector ServerStartLocation, FRotator ServerStartRotation, float ClientInterpolationSpeed);

	/** Delivers the authoritative final transform to the target owner. */
	UFUNCTION(Client, Reliable)
	void ClientFinishOwnerConfirmedReaction(FPP_ReactionPredictionContext Context, FGameplayTag ReactionTag,
		FVector ServerFinalLocation, FRotator ServerFinalRotation);

	/** Corrects only the attacking client's predicted target proxy. */
	UFUNCTION(Client, Unreliable)
	void ClientFinishPredictedProxyReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerFinalLocation, FRotator ServerFinalRotation);

	/** Reliably rolls back a prediction when no matching authoritative server collision arrives. */
	UFUNCTION(Client, Reliable)
	void ClientRejectPredictedReaction(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerLocation, FRotator ServerRotation);

	/** Reliably reconciles the predicting owner's proxy without making observer cosmetics reliable. */
	UFUNCTION(Client, Reliable)
	void ClientConfirmPredictedReactionStart(FPP_ReactionPredictionContext Context, AActor* TargetActor,
		FGameplayTag ReactionTag, FVector ServerStartLocation, FRotator ServerStartRotation);

	/** Reaction definitions used by predicted and confirmed playback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SyncPrediction|Reaction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UPP_ReactionData> ReactionData = nullptr;

	/** Latest predicted reaction time per target for replay throttling. */
	UPROPERTY(Transient)
	mutable TMap<TWeakObjectPtr<AActor>, double> LastReactionTimeByTarget;

	/** Latest accepted server reaction time per target. */
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AActor>, double> LastServerReactionTimeByTarget;

	/** Start of the current server request-rate window. */
	UPROPERTY(Transient)
	double ServerReactionRequestWindowStartSeconds = -1.0;

	/** Requests received during the current server rate window. */
	UPROPERTY(Transient)
	int32 ServerReactionRequestsInCurrentWindow = 0;

	/** Next local prediction identifier before wrapping. */
	UPROPERTY(Transient)
	int32 NextPredictionId = 0;

	static constexpr int32 MaxPredictionId = 32767;
	static constexpr int32 MaxPendingPredictedReactions = 32;
	static constexpr int32 MaxPendingServerReactionRequests = 32;
	static constexpr int32 MaxAuthoritativeCollisionRecords = 64;

	/** Local predictions waiting for server start confirmation. */
	UPROPERTY(Transient)
	TArray<FPP_PendingPredictedReaction> PendingPredictedReactions;

	/** Confirmed predictions waiting for server final transforms. */
	UPROPERTY(Transient)
	TArray<FPP_DeferredPredictedReactionCorrection> DeferredPredictedReactionCorrections;

	/** Final corrections waiting for locally controlled targets. */
	UPROPERTY(Transient)
	TArray<FPP_OwnerPendingFinalReactionCorrection> OwnerPendingFinalReactionCorrections;

	/** Final corrections waiting for locally predicted proxies. */
	UPROPERTY(Transient)
	TArray<FPP_OwnerPendingFinalReactionCorrection> ProxyPendingFinalReactionCorrections;

	/** Server notify hits waiting for the corresponding reliable client request. */
	UPROPERTY(Transient)
	TArray<FPP_AuthoritativeCollisionRecord> AuthoritativeCollisionRecords;

	/** Client requests that arrived before the server montage reached its collision window. */
	UPROPERTY(Transient)
	TArray<FPP_PendingServerReactionRequest> PendingServerReactionRequests;

	/** Notify-time matches queued to start before the target movement tick on the following frame. */
	TArray<FPP_DeferredServerReactionFromCollision> DeferredServerReactionsFromCollision;

	/** Number of overlapping owner reactions suppressing montage-track correction. */
	UPROPERTY(Transient)
	int32 OwnerReactionCorrectionSuppressionCount = 0;

	/** Owner reactions waiting briefly for ordinary Character Movement replication to settle. */
	TArray<FPP_OwnerFinalReconciliationGraceState> OwnerFinalReconciliationGraceStates;

	/** Active predicted reactions that temporarily ignore target capsule collision. */
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AActor>, int32> PredictedReactionCollisionIgnoreCounts;

	/** Active client-only smoothing timers keyed by reaction recipient. */
	TMap<TWeakObjectPtr<AActor>, FTimerHandle> ClientReactionMovementInterpolationTimers;

	/** Simulated proxy meshes currently offset from their authoritative capsules. */
	TMap<TWeakObjectPtr<AActor>, FPP_ProxyReactionVisualInterpolation> ProxyReactionVisualInterpolations;

	/** Confirmed proxy reactions suppressing intermediate server root-motion replay on the predicting client. */
	TArray<FPP_PredictedProxyRootMotionReconciliation> PredictedProxyRootMotionReconciliations;

	/** Maximum reaction requests accepted from one client each second. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Server Validation", meta=(ClampMin="1"))
	int32 MaxServerReactionRequestsPerSecond = 30;

	/** Maximum attacker-to-target distance accepted by the server. Zero disables this check. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Server Validation", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxServerReactionRequestDistance = 1200.0f;

	/** Maximum forward movement accepted from a notify transform request. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Server Validation", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxServerReactionMoveDistance = 500.0f;

	/** Maximum absolute sideways movement accepted from a notify transform request. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Server Validation", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxServerReactionLateralOffset = 500.0f;

	/** Require a matching server notify collision before accepting a predicted reaction request. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Server Validation")
	bool bRequireAuthoritativeCollisionMatch = true;

	/** Time allowed for the server hit and client request to arrive in either order. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Server Validation",
		meta=(ClampMin="0.1", ClampMax="3.0", Units="Seconds"))
	float AuthoritativeCollisionMatchTimeout = 1.0f;

	/** Time an unconfirmed local prediction remains matchable. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float PendingPredictedReactionTimeout = 2.0f;

	/** Time confirmed and final-correction markers remain valid. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Seconds"))
	float DeferredPredictedCorrectionTimeout = 2.0f;

	/** Location error ignored when checking the server's final transform. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float FinalCorrectionTolerance = 2.0f;

	/** Rotation error ignored when checking the server's final transform. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Degrees"))
	float FinalRotationCorrectionTolerance = 1.0f;

	/** Snaps predicted proxies to the server's final transform when needed. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction")
	bool bApplyInstantFinalCorrection = true;

	/** Maximum proxy error eligible for an instant final correction. Zero disables the limit. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float MaxInstantFinalCorrectionDistance = 500.0f;

	/** Short delay that lets proxy montage movement settle before correction. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", ClampMax="0.25", Units="Seconds"))
	float ProxyFinalCorrectionDelaySeconds = 0.05f;

	/** Blend time used to move the local target to the server's final transform. */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", ClampMax="0.5", Units="Seconds"))
	float OwnerFinalCorrectionSmoothSeconds = 0.15f;

	/**
	 * An early final must exceed this gap before owner correction is smoothed while ordinary movement
	 * corrections remain suppressed. Smaller gaps keep the existing replication-settle path.
	 */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float OwnerFinalPreReleaseSmoothMinDistance = 8.0f;

	/**
	 * Ignore missing initial montage displacement below this distance. Small differences are normal
	 * replication noise and correcting them would disturb starts that already look smooth.
	 */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float ServerRootMotionStartAlignmentMinDistance = 8.0f;

	/**
	 * Maximum missing initial montage displacement repaired on the server. Larger differences are
	 * treated as stale/invalid state rather than one skipped root-motion slice. Accepted repairs use
	 * a collision sweep and therefore cannot move the target through blocking geometry.
	 */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", Units="Centimeters"))
	float ServerRootMotionStartAlignmentMaxDistance = 75.0f;

	/**
	 * Applied only when the owner reaction has just ended. It gives normal movement replication time to
	 * consume its last updates before measuring the queued final transform. Finals arriving later bypass it.
	 */
	UPROPERTY(EditAnywhere, Category="SyncPrediction|Reaction", meta=(ClampMin="0.0", ClampMax="0.25", Units="Seconds"))
	float OwnerFinalReconciliationGraceSeconds = 0.075f;
};
