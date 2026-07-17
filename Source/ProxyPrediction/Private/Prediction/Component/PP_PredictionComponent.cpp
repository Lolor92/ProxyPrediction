#include "Prediction/Component/PP_PredictionComponent.h"
#include "Combat/Component/PP_CombatComponent.h"
#include "TimerManager.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "GAS/Ability/PP_GameplayAbility.h"
#include "PhysicsEngine/BodyInstance.h"
#include "AbilityMotion/Movement/PP_CharacterMovementComponent.h"
#include "AnimInstance/PP_AnimInstance.h"
#include "Tag/PP_NativeTags.h"
#include "UI/PP_CombatTextComponent.h"
#include "UI/PP_CombatTextTypes.h"

namespace
{
	constexpr double PPPredictedRootMotionReconciliationTimeoutSeconds = 1.0;
	constexpr double PPPredictedMontageRecoveryEndGuardSeconds = 0.2;
	constexpr int32 PPMaxServerCharacterHistorySamples = 128;

	bool PP_IsAdditiveReaction(const FPP_ReactionDataEntry& Reaction)
	{
		// Additive montages overlay the current pose and must never own capsule reconciliation.
		return Reaction.Montage && Reaction.Montage->IsValidAdditive();
	}

	float PP_GetRotationDistance(const FRotator& A, const FRotator& B)
	{
		return FMath::Max3(
			FMath::Abs(FRotator::NormalizeAxis(A.Pitch - B.Pitch)),
			FMath::Abs(FRotator::NormalizeAxis(A.Yaw - B.Yaw)),
			FMath::Abs(FRotator::NormalizeAxis(A.Roll - B.Roll)));
	}

	template <typename TEnum>
	bool PP_IsValidReactionEnumValue(TEnum Value)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Value));
	}

	static void PP_SetOwnerMontageTrackCorrectionSuppressed(UCharacterMovementComponent* MovementComponent,
	 bool bSuppressed)
	{
		UPP_CharacterMovementComponent* SyncMoveComponent =
			Cast<UPP_CharacterMovementComponent>(MovementComponent);
		if (!SyncMoveComponent) return;

		SyncMoveComponent->SetIgnoreServerRootMotionMontageTrackCorrection(bSuppressed);
	}

}

UPP_PredictionComponent::UPP_PredictionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.0f;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UPP_PredictionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Character Movement already owns and replicates a server-authored transform timestamp.
	// Keep it enabled for every smoothing mode so predicted hits can identify the exact target
	// collision snapshot without adding another per-frame replicated property or RPC.
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MovementComponent =
			OwnerCharacter->GetCharacterMovement())
		{
			MovementComponent->bNetworkAlwaysReplicateTransformUpdateTimestamp = true;
		}
	}

	if (GetOwner() && GetOwner()->HasAuthority() && bEnableServerHitLagCompensation)
	{
		RecordServerOwnerHistory();
		SetComponentTickEnabled(true);
	}
}

void UPP_PredictionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAllClientReactionMovementInterpolations();
	for (const FPP_PendingPredictedReaction& Pending : PendingPredictedReactions)
	{
		EndPredictedProxyTargetEffectFeedback(
			Pending.TargetActor.Get(), Pending.PredictedProxyAnimTagHandle);
	}
	PendingPredictedReactions.Reset();
	ServerCharacterHistory.Reset();
	LastServerHistorySampleTimeSeconds = -1.0;
	AuthoritativeCollisionRecords.Reset();
	AuthoritativeCollisionSweepRecords.Reset();
	PendingServerReactionRequests.Reset();
	for (const FPP_DeferredServerReactionFromCollision& State : DeferredServerReactionsFromCollision)
	{
		if (UCharacterMovementComponent* MovementComponent = State.MovementComponent.Get())
		{
			MovementComponent->RemoveTickPrerequisiteComponent(this);
		}
	}
	DeferredServerReactionsFromCollision.Reset();
	for (const FPP_OwnerFinalReconciliationGraceState& State : OwnerFinalReconciliationGraceStates)
	{
		if (State.bHoldMovementCorrections)
		{
			if (UPP_CharacterMovementComponent* MovementComponent =
				Cast<UPP_CharacterMovementComponent>(State.MovementComponent.Get()))
			{
				MovementComponent->SetIgnoreServerRootMotionMontageTrackCorrection(false);
			}
		}
	}
	OwnerFinalReconciliationGraceStates.Reset();
	while (!PredictedProxyRootMotionReconciliations.IsEmpty())
	{
		RemovePredictedProxyRootMotionReconciliation(
			PredictedProxyRootMotionReconciliations.Num() - 1);
	}
	Super::EndPlay(EndPlayReason);
}

void UPP_PredictionComponent::TickComponent(float DeltaTime, ELevelTick TickType, 
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RecordServerOwnerHistory();
	ProcessDeferredServerReactionsFromCollision();
	UpdatePredictedProxyRootMotionReconciliations();

	if (PredictedProxyRootMotionReconciliations.IsEmpty() &&
		DeferredServerReactionsFromCollision.IsEmpty() &&
		(!GetOwner() || !GetOwner()->HasAuthority() || !bEnableServerHitLagCompensation))
	{
		SetComponentTickEnabled(false);
	}
}

void UPP_PredictionComponent::RecordServerOwnerHistory()
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!bEnableServerHitLagCompensation || !OwnerActor || !OwnerActor->HasAuthority() ||
		!World || !Cast<ACharacter>(OwnerActor))
	{
		return;
	}

	const double NowSeconds = World->GetTimeSeconds();
	const double SampleInterval = FMath::Max(0.008f, ServerHistorySampleInterval);
	if (LastServerHistorySampleTimeSeconds >= 0.0 &&
		NowSeconds - LastServerHistorySampleTimeSeconds < SampleInterval)
	{
		return;
	}

	FPP_ServerCharacterHistorySample& Sample = ServerCharacterHistory.AddDefaulted_GetRef();
	Sample.ServerTimeSeconds = NowSeconds;
	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerActor))
	{
		if (const UCharacterMovementComponent* MovementComponent =
			OwnerCharacter->GetCharacterMovement())
		{
			Sample.MovementTimeStamp = MovementComponent->GetServerLastTransformUpdateTimeStamp();
		}
	}
	Sample.Location = OwnerActor->GetActorLocation();
	Sample.Rotation = OwnerActor->GetActorRotation();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActor(OwnerActor))
	{
		UGameplayAbility* AnimatingAbility = ASC->GetAnimatingAbility();
		Sample.AnimatingAbility = AnimatingAbility;
		Sample.AnimatingMontage = AnimatingAbility ? AnimatingAbility->GetCurrentMontage() : nullptr;
	}
	LastServerHistorySampleTimeSeconds = NowSeconds;

	const double OldestAllowedTime = NowSeconds - FMath::Max(0.1f, MaxServerRewindSeconds) - SampleInterval;
	int32 RemoveCount = 0;
	while (RemoveCount < ServerCharacterHistory.Num() &&
		ServerCharacterHistory[RemoveCount].ServerTimeSeconds < OldestAllowedTime)
	{
		++RemoveCount;
	}
	if (RemoveCount > 0)
	{
		ServerCharacterHistory.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
	if (ServerCharacterHistory.Num() > PPMaxServerCharacterHistorySamples)
	{
		ServerCharacterHistory.RemoveAt(
			0, ServerCharacterHistory.Num() - PPMaxServerCharacterHistorySamples,
			EAllowShrinking::No);
	}
}

bool UPP_PredictionComponent::TrySampleServerOwnerHistory(
	const double ServerTimeSeconds,
	FPP_ServerCharacterHistorySample& OutSample) const
{
	if (!FMath::IsFinite(ServerTimeSeconds) || ServerCharacterHistory.IsEmpty())
	{
		return false;
	}

	const double EndpointTolerance = FMath::Max(0.008f, ServerHistorySampleInterval) * 1.5;
	const FPP_ServerCharacterHistorySample& First = ServerCharacterHistory[0];
	const FPP_ServerCharacterHistorySample& Last = ServerCharacterHistory.Last();
	if (ServerTimeSeconds < First.ServerTimeSeconds - EndpointTolerance ||
		ServerTimeSeconds > Last.ServerTimeSeconds + EndpointTolerance)
	{
		return false;
	}
	if (ServerTimeSeconds <= First.ServerTimeSeconds)
	{
		OutSample = First;
		return true;
	}
	if (ServerTimeSeconds >= Last.ServerTimeSeconds)
	{
		OutSample = Last;
		return true;
	}

	for (int32 Index = 1; Index < ServerCharacterHistory.Num(); ++Index)
	{
		const FPP_ServerCharacterHistorySample& Next = ServerCharacterHistory[Index];
		if (Next.ServerTimeSeconds < ServerTimeSeconds)
		{
			continue;
		}

		const FPP_ServerCharacterHistorySample& Previous = ServerCharacterHistory[Index - 1];
		const double Span = FMath::Max(UE_DOUBLE_SMALL_NUMBER,
			Next.ServerTimeSeconds - Previous.ServerTimeSeconds);
		const float Alpha = static_cast<float>(FMath::Clamp(
			(ServerTimeSeconds - Previous.ServerTimeSeconds) / Span, 0.0, 1.0));
		OutSample.ServerTimeSeconds = ServerTimeSeconds;
		OutSample.MovementTimeStamp = Alpha < 0.5f
			? Previous.MovementTimeStamp : Next.MovementTimeStamp;
		OutSample.Location = FMath::Lerp(Previous.Location, Next.Location, Alpha);
		OutSample.Rotation = FQuat::Slerp(
			Previous.Rotation.Quaternion(), Next.Rotation.Quaternion(), Alpha).Rotator();
		// Treat an ability as active only after a server sample actually observed it.
		OutSample.AnimatingAbility = Previous.AnimatingAbility;
		OutSample.AnimatingMontage = Previous.AnimatingMontage;
		return true;
	}

	return false;
}

bool UPP_PredictionComponent::TryResolveServerOwnerHistoryFromMovementTimeStamp(
	const float MovementTimeStamp,
	const double ReferenceHitTimeSeconds,
	FPP_ServerCharacterHistorySample& OutSample) const
{
	if (!FMath::IsFinite(MovementTimeStamp) || MovementTimeStamp <= 0.0f ||
		!FMath::IsFinite(ReferenceHitTimeSeconds) || ServerCharacterHistory.IsEmpty())
	{
		return false;
	}

	const FPP_ServerCharacterHistorySample* BestSample = nullptr;
	float BestTimestampDelta = TNumericLimits<float>::Max();
	double BestReferenceDelta = TNumericLimits<double>::Max();
	for (const FPP_ServerCharacterHistorySample& Sample : ServerCharacterHistory)
	{
		if (!FMath::IsFinite(Sample.MovementTimeStamp) || Sample.MovementTimeStamp <= 0.0f)
		{
			continue;
		}

		const float TimestampDelta = FMath::Abs(Sample.MovementTimeStamp - MovementTimeStamp);
		const double ReferenceDelta = FMath::Abs(Sample.ServerTimeSeconds - ReferenceHitTimeSeconds);
		if (TimestampDelta < BestTimestampDelta ||
			(FMath::IsNearlyEqual(TimestampDelta, BestTimestampDelta, UE_KINDA_SMALL_NUMBER) &&
				ReferenceDelta < BestReferenceDelta))
		{
			BestSample = &Sample;
			BestTimestampDelta = TimestampDelta;
			BestReferenceDelta = ReferenceDelta;
		}
	}

	if (!BestSample ||
		BestTimestampDelta > FMath::Max(0.001f, TargetPresentationTimestampMatchTolerance))
	{
		return false;
	}

	OutSample = *BestSample;
	return true;
}

bool UPP_PredictionComponent::ValidateClientHitTime(FPP_ReactionPredictionContext& Context) const
{
	Context.bServerHitTimeValidated = false;
	Context.ServerValidatedHitTimeSeconds = -1.0;

	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (!bEnableServerHitLagCompensation || !OwnerActor || !OwnerActor->HasAuthority() || !World ||
		!FMath::IsFinite(Context.ClientEstimatedServerTimeSeconds) ||
		Context.ClientEstimatedServerTimeSeconds < 0.0)
	{
		return false;
	}

	const double NowSeconds = World->GetTimeSeconds();
	const double MaxRewind = FMath::Max(0.1f, MaxServerRewindSeconds);
	const double FutureTolerance = FMath::Max(0.0f, FutureHitTimeToleranceSeconds);
	const double RequestedTime = Context.ClientEstimatedServerTimeSeconds;
	if (RequestedTime < NowSeconds - MaxRewind || RequestedTime > NowSeconds + FutureTolerance)
	{

		return false;
	}

	Context.ServerValidatedHitTimeSeconds = FMath::Clamp(RequestedTime, NowSeconds - MaxRewind, NowSeconds);
	Context.bServerHitTimeValidated = true;

	return true;
}

bool UPP_PredictionComponent::ValidateTargetPresentationTime(
	FPP_ReactionPredictionContext& Context,
	AActor* TargetActor) const
{
	// These are server-owned output fields. Never accept values serialized by the client.
	Context.bServerTargetPresentationTimeValidated = false;
	Context.ServerValidatedTargetPresentationTimeSeconds = -1.0;

	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (!bEnableServerHitLagCompensation || !Context.bServerHitTimeValidated ||
		!OwnerActor || !OwnerActor->HasAuthority() || !World || !TargetActor ||
		TargetActor == OwnerActor || TargetActor->GetWorld() != World)
	{
		return false;
	}
	if (!FMath::IsFinite(Context.TargetPresentationMovementTimeStamp) ||
		Context.TargetPresentationMovementTimeStamp <= 0.0f)
	{

		return false;
	}

	const UPP_PredictionComponent* TargetPredictionComponent =
		TargetActor->FindComponentByClass<UPP_PredictionComponent>();
	FPP_ServerCharacterHistorySample PresentationSample;
	if (!TargetPredictionComponent ||
		!TargetPredictionComponent->TryResolveServerOwnerHistoryFromMovementTimeStamp(
			Context.TargetPresentationMovementTimeStamp,
			Context.ServerValidatedHitTimeSeconds,
			PresentationSample))
	{

		return false;
	}

	const double PresentationAgeBeforeHit =
		Context.ServerValidatedHitTimeSeconds - PresentationSample.ServerTimeSeconds;
	const double NowSeconds = World->GetTimeSeconds();
	const double MaxPresentationAge =
		FMath::Max(0.05f, MaxTargetPresentationAgeBeforeHitSeconds);
	const double FutureTolerance = FMath::Max(0.0f, FutureHitTimeToleranceSeconds);
	const double MaxRewind = FMath::Max(0.1f, MaxServerRewindSeconds);
	if (PresentationAgeBeforeHit < -FutureTolerance ||
		PresentationAgeBeforeHit > MaxPresentationAge ||
		PresentationSample.ServerTimeSeconds < NowSeconds - MaxRewind ||
		PresentationSample.ServerTimeSeconds > NowSeconds + FutureTolerance)
	{

		return false;
	}

	Context.ServerValidatedTargetPresentationTimeSeconds = PresentationSample.ServerTimeSeconds;
	Context.bServerTargetPresentationTimeValidated = true;

	return true;
}

double UPP_PredictionComponent::GetValidatedTargetHistoryTimeSeconds(
	const FPP_ReactionPredictionContext& Context) const
{
	return Context.bServerTargetPresentationTimeValidated &&
		FMath::IsFinite(Context.ServerValidatedTargetPresentationTimeSeconds) &&
		Context.ServerValidatedTargetPresentationTimeSeconds >= 0.0
			? Context.ServerValidatedTargetPresentationTimeSeconds
			: Context.ServerValidatedHitTimeSeconds;
}

bool UPP_PredictionComponent::ApplyLagCompensatedRootMotionRollback(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor) const
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!bEnableServerHitLagCompensation || !bRequireAuthoritativeCollisionMatch ||
		!Context.bServerHitTimeValidated || !OwnerActor || !OwnerActor->HasAuthority() ||
		!World || !TargetActor)
	{
		return false;
	}

	const UPP_PredictionComponent* TargetPredictionComponent =
		TargetActor->FindComponentByClass<UPP_PredictionComponent>();
	const double TargetHistoryTimeSeconds = GetValidatedTargetHistoryTimeSeconds(Context);
	FPP_ServerCharacterHistorySample HistoricalSample;
	if (!TargetPredictionComponent || !TargetPredictionComponent->TrySampleServerOwnerHistory(
		TargetHistoryTimeSeconds, HistoricalSample))
	{

		return false;
	}

	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	UGameplayAbility* CurrentAbility = TargetASC ? TargetASC->GetAnimatingAbility() : nullptr;
	UAnimMontage* CurrentMontage = CurrentAbility ? CurrentAbility->GetCurrentMontage() : nullptr;
	if (!CurrentAbility || !CurrentMontage || !CurrentMontage->HasRootMotion())
	{

		return false;
	}

	const FVector CurrentLocation = TargetActor->GetActorLocation();
	const float RollbackDistance = FVector::Dist2D(CurrentLocation, HistoricalSample.Location);
	const float MinDistance = FMath::Max(0.0f, MinLagCompensationRollbackDistance);
	const float MaxDistance = FMath::Max(0.0f, MaxLagCompensationRollbackDistance);
	if (RollbackDistance <= MinDistance || (MaxDistance > 0.0f && RollbackDistance > MaxDistance))
	{

		return false;
	}

	// The server, not the client, identified both the historical transform and the currently active
	// root-motion ability. This also applies when that same ability was already active at hit time:
	// preserve movement consumed before the hit, rewind movement consumed afterward, then cancel the
	// remaining root motion so it cannot be consumed again on the next movement tick.
	TargetASC->CancelAbility(CurrentAbility);
	if (ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		if (UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement())
		{
			MovementComponent->StopMovementImmediately();
		}
	}

	FHitResult SweepResult;
	TargetActor->SetActorLocationAndRotation(
		HistoricalSample.Location, HistoricalSample.Rotation,
		true, &SweepResult, ETeleportType::TeleportPhysics);
	const FVector AppliedLocation = TargetActor->GetActorLocation();

	return FVector::DistSquared2D(CurrentLocation, AppliedLocation) > FMath::Square(KINDA_SMALL_NUMBER);
}

bool UPP_PredictionComponent::TryValidateHistoricalCollisionSweep(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	const FPP_AuthoritativeCollisionSweepRecord& SweepRecord,
	FHitResult& OutHitResult,
	const TCHAR*& OutFailureReason) const
{
	OutFailureReason = TEXT("InvalidState");
	AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (!bEnableServerHitLagCompensation || !Context.bServerHitTimeValidated ||
		!OwnerActor || !OwnerActor->HasAuthority() || !World || !TargetActor ||
		!SweepRecord.ReactionTag.IsValid())
	{
		return false;
	}

	const double SweepTimeDelta = SweepRecord.TimeSeconds - Context.ServerValidatedHitTimeSeconds;
	if (!FMath::IsFinite(SweepTimeDelta) ||
		FMath::Abs(SweepTimeDelta) > FMath::Max(0.05f, MaxHistoricalCollisionSweepTimeDelta))
	{
		OutFailureReason = TEXT("SweepTimeDelta");
		return false;
	}

	FPP_ServerCharacterHistorySample HistoricalAttackerSample;
	if (!TrySampleServerOwnerHistory(
		Context.ServerValidatedHitTimeSeconds, HistoricalAttackerSample))
	{
		OutFailureReason = TEXT("NoAttackerHistory");
		return false;
	}

	const UPP_PredictionComponent* TargetPredictionComponent =
		TargetActor->FindComponentByClass<UPP_PredictionComponent>();
	const double TargetHistoryTimeSeconds = GetValidatedTargetHistoryTimeSeconds(Context);
	FPP_ServerCharacterHistorySample HistoricalTargetSample;
	if (!TargetPredictionComponent || !TargetPredictionComponent->TrySampleServerOwnerHistory(
		TargetHistoryTimeSeconds, HistoricalTargetSample))
	{
		OutFailureReason = TEXT("NoTargetHistory");
		return false;
	}

	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	UCapsuleComponent* TargetCapsule = TargetCharacter ? TargetCharacter->GetCapsuleComponent() : nullptr;
	FBodyInstance* TargetBodyInstance = TargetCapsule ? TargetCapsule->GetBodyInstance() : nullptr;
	if (!TargetBodyInstance || !TargetBodyInstance->IsValidBodyInstance())
	{
		OutFailureReason = TEXT("NoTargetBody");
		return false;
	}

	const FTransform HistoricalAttackerTransform(
		HistoricalAttackerSample.Rotation, HistoricalAttackerSample.Location);
	const FTransform HistoricalTargetTransform(
		HistoricalTargetSample.Rotation, HistoricalTargetSample.Location);
	const FTransform CurrentTargetTransform = TargetActor->GetActorTransform();

	// The notify geometry is authored and evaluated by the server at the correct montage phase.
	// Move it from the server attacker's current frame into the attacker's synchronized historical
	// frame, then express the historical attacker/target relationship around the target's current
	// physics body. Rigid-transform invariance lets BodyInstance perform the exact sweep without
	// teleporting the live target or firing overlap callbacks.
	const FTransform HistoricalTraceStart =
		SweepRecord.TraceStartTransform.GetRelativeTransform(SweepRecord.AttackerTransform) *
		HistoricalAttackerTransform;
	const FTransform HistoricalTraceEnd =
		SweepRecord.TraceEndTransform.GetRelativeTransform(SweepRecord.AttackerTransform) *
		HistoricalAttackerTransform;
	const FTransform EquivalentCurrentTraceStart =
		HistoricalTraceStart.GetRelativeTransform(HistoricalTargetTransform) * CurrentTargetTransform;
	const FTransform EquivalentCurrentTraceEnd =
		HistoricalTraceEnd.GetRelativeTransform(HistoricalTargetTransform) * CurrentTargetTransform;

	FHitResult EquivalentHit;
	if (!TargetBodyInstance->Sweep(
		EquivalentHit,
		EquivalentCurrentTraceStart.GetLocation(),
		EquivalentCurrentTraceEnd.GetLocation(),
		EquivalentCurrentTraceEnd.GetRotation().GetNormalized(),
		SweepRecord.CollisionShape,
		false))
	{
		OutFailureReason = TEXT("GeometryMiss");
		return false;
	}

	const FVector HistoricalImpactPoint = HistoricalTargetTransform.TransformPosition(
		CurrentTargetTransform.InverseTransformPosition(EquivalentHit.ImpactPoint));
	const FVector HistoricalImpactNormal = HistoricalTargetTransform.TransformVectorNoScale(
		CurrentTargetTransform.InverseTransformVectorNoScale(EquivalentHit.ImpactNormal)).GetSafeNormal();
	OutHitResult = FHitResult(
		TargetActor, TargetCapsule, HistoricalImpactPoint, HistoricalImpactNormal);
	OutHitResult.TraceStart = HistoricalTraceStart.GetLocation();
	OutHitResult.TraceEnd = HistoricalTraceEnd.GetLocation();
	OutHitResult.Location = HistoricalTargetTransform.TransformPosition(
		CurrentTargetTransform.InverseTransformPosition(EquivalentHit.Location));
	OutHitResult.Time = EquivalentHit.Time;
	OutHitResult.Distance = FVector::Distance(OutHitResult.TraceStart, OutHitResult.Location);
	OutHitResult.bStartPenetrating = EquivalentHit.bStartPenetrating;
	OutHitResult.PenetrationDepth = EquivalentHit.PenetrationDepth;
	OutFailureReason = TEXT("None");
	return true;
}

bool UPP_PredictionComponent::TryFindHistoricalCollisionSweep(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	FPP_ReactionTransformSettings& OutTransformSettings,
	FPP_ReactionDefenseSettings& OutDefenseSettings,
	FPP_ReactionDamageSettings& OutDamageSettings,
	FPP_ReactionGameplayCueSettings& OutGameplayCueSettings,
	FHitResult& OutHitResult)
{
	if (!bEnableServerHitLagCompensation || !Context.bServerHitTimeValidated ||
		!TargetActor || !ReactionTag.IsValid())
	{
		return false;
	}

	RemoveExpiredAuthoritativeCollisionState();
	TArray<int32, TInlineAllocator<16>> CandidateIndices;
	for (int32 Index = 0; Index < AuthoritativeCollisionSweepRecords.Num(); ++Index)
	{
		const FPP_AuthoritativeCollisionSweepRecord& Record =
			AuthoritativeCollisionSweepRecords[Index];
		if (Record.ReactionTag == ReactionTag &&
			!Record.ValidatedTargets.Contains(TWeakObjectPtr<AActor>(TargetActor)))
		{
			CandidateIndices.Add(Index);
		}
	}

	CandidateIndices.Sort(
		[this, &Context](const int32 LeftIndex, const int32 RightIndex)
		{
			const double LeftDelta = FMath::Abs(
				AuthoritativeCollisionSweepRecords[LeftIndex].TimeSeconds -
				Context.ServerValidatedHitTimeSeconds);
			const double RightDelta = FMath::Abs(
				AuthoritativeCollisionSweepRecords[RightIndex].TimeSeconds -
				Context.ServerValidatedHitTimeSeconds);
			return LeftDelta < RightDelta;
		});

	const TCHAR* LastFailureReason = TEXT("NoCandidateSweep");
	for (const int32 CandidateIndex : CandidateIndices)
	{
		FPP_AuthoritativeCollisionSweepRecord& Record =
			AuthoritativeCollisionSweepRecords[CandidateIndex];
		if (!TryValidateHistoricalCollisionSweep(
			Context, TargetActor, Record, OutHitResult, LastFailureReason))
		{
			continue;
		}

		Record.ValidatedTargets.Add(TargetActor);
		OutTransformSettings = Record.TransformSettings;
		OutDefenseSettings = Record.DefenseSettings;
		OutDamageSettings = Record.DamageSettings;
		OutGameplayCueSettings = Record.GameplayCueSettings;

		return true;
	}


	return false;
}

void UPP_PredictionComponent::BeginPredictedProxyRootMotionReconciliation(const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag, const bool bServerConfirmed)
{
	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	UWorld* World = GetWorld();
	if (!World || !Context.IsValid() || !TargetCharacter || TargetCharacter->IsLocallyControlled() ||
		!ReactionData || !ReactionTag.IsValid())
	{
		return;
	}

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction) || !Reaction.Montage ||
		PP_IsAdditiveReaction(Reaction))
	{
		return;
	}

	TOptional<FSimulatedRootMotionReplicatedMove> CarriedAuthoritativeMove;
	float CarriedAuthoritativePlayRate = 0.0f;
	// A confirmation for an older hit must never displace protection owned by a newer prediction.
	for (int32 Index = PredictedProxyRootMotionReconciliations.Num() - 1; Index >= 0; --Index)
	{
		FPP_PredictedProxyRootMotionReconciliation& Existing =
			PredictedProxyRootMotionReconciliations[Index];
		if (Existing.TargetCharacter.Get() != TargetCharacter)
		{
			continue;
		}

		if (Existing.PredictionId == Context.PredictionId)
		{
			Existing.bServerConfirmed |= bServerConfirmed;
			if (bServerConfirmed && Existing.QuarantinedAuthoritativeMove.IsSet())
			{

				Existing.QuarantinedAuthoritativeMove.Reset();
				Existing.QuarantinedAuthoritativePlayRate = 0.0f;
			}

			EnsurePredictedProxyReactionMontagePlaying(Context, TargetActor, Reaction);
			return;
		}

		if (bServerConfirmed && Existing.PredictionId > Context.PredictionId)
		{

			return;
		}

		if (Existing.QuarantinedAuthoritativeMove.IsSet() &&
			(!CarriedAuthoritativeMove.IsSet() ||
			 Existing.QuarantinedAuthoritativeMove->Time >= CarriedAuthoritativeMove->Time))
		{
			CarriedAuthoritativeMove = Existing.QuarantinedAuthoritativeMove;
			CarriedAuthoritativePlayRate = Existing.QuarantinedAuthoritativePlayRate;
		}
		RemovePredictedProxyRootMotionReconciliation(Index);
	}

	FPP_PredictedProxyRootMotionReconciliation& State =
		PredictedProxyRootMotionReconciliations.AddDefaulted_GetRef();
	State.TargetCharacter = TargetCharacter;
	State.Montage = Reaction.Montage;
	State.ReactionTag = ReactionTag;
	State.PredictionId = Context.PredictionId;
	State.bServerConfirmed = bServerConfirmed;
	if (!bServerConfirmed)
	{
		State.QuarantinedAuthoritativeMove = MoveTemp(CarriedAuthoritativeMove);
		State.QuarantinedAuthoritativePlayRate = CarriedAuthoritativePlayRate;
	}
	RefreshPredictedProxyReconciliationAnimationState(TargetCharacter);
	const USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
	const UAnimInstance* AnimInstance = TargetMesh ? TargetMesh->GetAnimInstance() : nullptr;
	const float CurrentMontagePosition = AnimInstance
		? AnimInstance->Montage_GetPosition(Reaction.Montage)
		: GetReactionStartPosition(Reaction);
	const float ObservedMontagePlayRate = AnimInstance
		? FMath::Abs(AnimInstance->Montage_GetPlayRate(Reaction.Montage))
		: 0.0f;
	const float MontagePlayRate = ObservedMontagePlayRate > KINDA_SMALL_NUMBER
		? ObservedMontagePlayRate
		: FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
	const double RemainingMontageSeconds =
		FMath::Max(0.0f, Reaction.Montage->GetPlayLength() - CurrentMontagePosition) / MontagePlayRate;
	State.StartPosition = CurrentMontagePosition;
	State.MontagePlayRate = MontagePlayRate;
	State.StartTimeSeconds = World->GetTimeSeconds();
	State.ExpectedEndTimeSeconds = State.StartTimeSeconds + RemainingMontageSeconds;
	State.ExpireTimeSeconds = State.StartTimeSeconds + FMath::Max(
		PPPredictedRootMotionReconciliationTimeoutSeconds, RemainingMontageSeconds + 0.5);

	if (UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement())
	{
		// Ensure replicated root-motion replay is suppressed before Character Movement can consume it.
		MovementComponent->AddTickPrerequisiteComponent(this);
	}

	const UAnimSequenceBase* ExpectedReplicatedAnimation = Reaction.Montage->IsDynamicMontage()
		? Reaction.Montage->GetFirstAnimReference()
		: Reaction.Montage;
	if (!bServerConfirmed)
	{
		for (const FSimulatedRootMotionReplicatedMove& Move : TargetCharacter->RootMotionRepMoves)
		{
			if (Move.RootMotion.Animation.Get() != ExpectedReplicatedAnimation)
			{
				QuarantineAuthoritativeProxyRootMotionMove(State, Move);
			}
		}
	}
	TargetCharacter->RootMotionRepMoves.Reset();

	SetComponentTickEnabled(true);
}

bool UPP_PredictionComponent::EnsurePredictedProxyReactionMontagePlaying(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	const FPP_ReactionDataEntry& Reaction)
{
	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	UWorld* World = GetWorld();
	if (!Context.IsValid() || !TargetCharacter || !World || !Reaction.Montage)
	{
		return false;
	}

	for (FPP_PredictedProxyRootMotionReconciliation& State : PredictedProxyRootMotionReconciliations)
	{
		if (State.TargetCharacter.Get() != TargetCharacter ||
			State.PredictionId != Context.PredictionId || State.Montage.Get() != Reaction.Montage)
		{
			continue;
		}

		UAnimInstance* AnimInstance = TargetCharacter->GetMesh()
			? TargetCharacter->GetMesh()->GetAnimInstance()
			: nullptr;
		if (AnimInstance && AnimInstance->Montage_IsPlaying(Reaction.Montage))
		{
			return true;
		}

		const double NowSeconds = World->GetTimeSeconds();
		if (NowSeconds >= State.ExpectedEndTimeSeconds - PPPredictedMontageRecoveryEndGuardSeconds)
		{
			return false;
		}

		const float ElapsedSeconds = static_cast<float>(FMath::Max(0.0, NowSeconds - State.StartTimeSeconds));
		const float RecoveryPosition = FMath::Clamp(
			State.StartPosition + (ElapsedSeconds * State.MontagePlayRate),
			0.0f,
			FMath::Max(0.0f, Reaction.Montage->GetPlayLength() - KINDA_SMALL_NUMBER));
		const bool bRestarted = PlayReactionMontageOnActor(
			TargetCharacter, Reaction, RecoveryPosition, true);

		return bRestarted;
	}

	return false;
}

void UPP_PredictionComponent::EndPredictedProxyRootMotionReconciliation(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor)
{
	if (!Context.IsValid() || !TargetActor)
	{
		return;
	}

	for (int32 Index = PredictedProxyRootMotionReconciliations.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PredictedProxyRootMotionReconciliation& State =
			PredictedProxyRootMotionReconciliations[Index];
		if (State.TargetCharacter.Get() == TargetActor && State.PredictionId == Context.PredictionId)
		{
			RemovePredictedProxyRootMotionReconciliation(Index);
			return;
		}
	}
}

void UPP_PredictionComponent::UpdatePredictedProxyRootMotionReconciliations()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		while (!PredictedProxyRootMotionReconciliations.IsEmpty())
		{
			RemovePredictedProxyRootMotionReconciliation(
				PredictedProxyRootMotionReconciliations.Num() - 1);
		}
		return;
	}

	const double NowSeconds = World->GetTimeSeconds();
	for (int32 StateIndex = PredictedProxyRootMotionReconciliations.Num() - 1; StateIndex >= 0; --StateIndex)
	{
		FPP_PredictedProxyRootMotionReconciliation& State =
			PredictedProxyRootMotionReconciliations[StateIndex];
		ACharacter* TargetCharacter = State.TargetCharacter.Get();
		UAnimMontage* Montage = State.Montage.Get();
		if (!TargetCharacter || !Montage || NowSeconds >= State.ExpireTimeSeconds)
		{
			RemovePredictedProxyRootMotionReconciliation(StateIndex);
			continue;
		}

		UAnimInstance* AnimInstance = TargetCharacter->GetMesh()
			? TargetCharacter->GetMesh()->GetAnimInstance()
			: nullptr;
		const bool bPredictedMontageStillPlaying =
			AnimInstance && AnimInstance->Montage_IsPlaying(Montage);
		const UAnimSequenceBase* ExpectedReplicatedAnimation = Montage->IsDynamicMontage()
			? Montage->GetFirstAnimReference()
			: Montage;
		int32 RemovedExpectedRepMoveCount = 0;
		int32 RemovedStaleRepMoveCount = 0;
		for (int32 MoveIndex = TargetCharacter->RootMotionRepMoves.Num() - 1; MoveIndex >= 0; --MoveIndex)
		{
			const FRepRootMotionMontage& RootMotion =
				TargetCharacter->RootMotionRepMoves[MoveIndex].RootMotion;
			const bool bExpectedReactionMove =
				RootMotion.Animation.Get() == ExpectedReplicatedAnimation;
			if (!bExpectedReactionMove && !State.bServerConfirmed)
			{
				QuarantineAuthoritativeProxyRootMotionMove(
					State, TargetCharacter->RootMotionRepMoves[MoveIndex]);
			}
			TargetCharacter->RootMotionRepMoves.RemoveAtSwap(MoveIndex, 1, EAllowShrinking::No);
			if (bExpectedReactionMove)
			{
				++RemovedExpectedRepMoveCount;
			}
			else
			{
				++RemovedStaleRepMoveCount;
			}
		}


		const bool bPredictedTimelineCompleted = NowSeconds >= State.ExpectedEndTimeSeconds;
		if (bPredictedTimelineCompleted && State.bServerConfirmed)
		{
			RemovePredictedProxyRootMotionReconciliation(StateIndex);
			continue;
		}

		// A stale montage may displace the prediction repeatedly, so recover whenever one is
		// filtered. Matching authoritative reaction replication can also stop the local montage
		// early; allow one recovery for that case, but never retry it near the natural blend-out.
		const bool bRecoverFromStaleMontage = RemovedStaleRepMoveCount > 0;
		const bool bRecoverOnceFromEarlyExpectedStop =
			State.bServerConfirmed && RemovedExpectedRepMoveCount > 0 &&
			!State.bRecoveredFromEarlyExpectedStop &&
			NowSeconds < State.ExpectedEndTimeSeconds - PPPredictedMontageRecoveryEndGuardSeconds;
		if (!bPredictedTimelineCompleted && !bPredictedMontageStillPlaying &&
			(bRecoverFromStaleMontage || bRecoverOnceFromEarlyExpectedStop))
		{
			FPP_ReactionDataEntry Reaction;
			if (ReactionData && ReactionData->FindReaction(State.ReactionTag, Reaction) &&
				Reaction.Montage == Montage)
			{
				FPP_ReactionPredictionContext Context;
				Context.PredictionId = State.PredictionId;
				const bool bRecovered = EnsurePredictedProxyReactionMontagePlaying(
					Context, TargetCharacter, Reaction);
				if (bRecovered && bRecoverOnceFromEarlyExpectedStop)
				{
					State.bRecoveredFromEarlyExpectedStop = true;
				}
			}
		}
	}
}

void UPP_PredictionComponent::RemovePredictedProxyRootMotionReconciliation(int32 Index)
{
	if (!PredictedProxyRootMotionReconciliations.IsValidIndex(Index)) return;

	ACharacter* TargetCharacter = PredictedProxyRootMotionReconciliations[Index].TargetCharacter.Get();

	if (TargetCharacter)
	{
		if (UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement())
		{
			MovementComponent->RemoveTickPrerequisiteComponent(this);
		}
	}

	PredictedProxyRootMotionReconciliations.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	RefreshPredictedProxyReconciliationAnimationState(TargetCharacter);
}

void UPP_PredictionComponent::QuarantineAuthoritativeProxyRootMotionMove(
	FPP_PredictedProxyRootMotionReconciliation& State,
	const FSimulatedRootMotionReplicatedMove& Move)
{
	if (!Move.RootMotion.bIsActive || !Move.RootMotion.Animation)
	{
		return;
	}

	if (State.QuarantinedAuthoritativeMove.IsSet() &&
		State.QuarantinedAuthoritativeMove->RootMotion.Animation == Move.RootMotion.Animation)
	{
		const float SampleDeltaSeconds = Move.Time - State.QuarantinedAuthoritativeMove->Time;
		const float SampleDeltaPosition =
			Move.RootMotion.Position - State.QuarantinedAuthoritativeMove->RootMotion.Position;
		if (SampleDeltaSeconds > UE_KINDA_SMALL_NUMBER)
		{
			const float CandidatePlayRate = SampleDeltaPosition / SampleDeltaSeconds;
			if (FMath::IsFinite(CandidatePlayRate) &&
				FMath::Abs(CandidatePlayRate) >= 0.1f && FMath::Abs(CandidatePlayRate) <= 5.0f)
			{
				State.QuarantinedAuthoritativePlayRate =
					FMath::Abs(State.QuarantinedAuthoritativePlayRate) > KINDA_SMALL_NUMBER
						? FMath::Lerp(State.QuarantinedAuthoritativePlayRate, CandidatePlayRate, 0.5f)
						: CandidatePlayRate;
			}
		}
	}

	if (!State.QuarantinedAuthoritativeMove.IsSet() ||
		Move.Time >= State.QuarantinedAuthoritativeMove->Time)
	{
		State.QuarantinedAuthoritativeMove = Move;
	}
}

bool UPP_PredictionComponent::TakeQuarantinedAuthoritativeProxyRootMotionMove(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	TOptional<FSimulatedRootMotionReplicatedMove>& OutMove,
	float& OutObservedPlayRate)
{
	OutMove.Reset();
	OutObservedPlayRate = 0.0f;
	if (!Context.IsValid() || !TargetActor)
	{
		return false;
	}

	for (FPP_PredictedProxyRootMotionReconciliation& State : PredictedProxyRootMotionReconciliations)
	{
		if (State.TargetCharacter.Get() != TargetActor || State.PredictionId != Context.PredictionId)
		{
			continue;
		}

		OutMove = MoveTemp(State.QuarantinedAuthoritativeMove);
		OutObservedPlayRate = State.QuarantinedAuthoritativePlayRate;
		State.QuarantinedAuthoritativeMove.Reset();
		State.QuarantinedAuthoritativePlayRate = 0.0f;
		return true;
	}

	return false;
}

bool UPP_PredictionComponent::RestoreAuthoritativeProxyStateAfterRejectedPrediction(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag PredictedReactionTag,
	TOptional<FSimulatedRootMotionReplicatedMove>&& QuarantinedMove,
	const float ObservedPlayRate)
{
	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActor(TargetActor);
	USkeletalMeshComponent* Mesh = TargetCharacter ? TargetCharacter->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Context.IsValid() || !TargetCharacter || TargetCharacter->IsLocallyControlled() ||
		!ASC || !AnimInstance)
	{
		return false;
	}

	if (ASC->HasMatchingGameplayTag(TAG_State_CC))
	{

		return false;
	}

	// A rejection may arrive after another local reaction has already taken ownership of this
	// proxy. Never let an older quarantined attack montage displace that newer reaction.
	for (const FPP_PredictedProxyRootMotionReconciliation& State :
		PredictedProxyRootMotionReconciliations)
	{
		if (State.TargetCharacter.Get() == TargetCharacter &&
			State.PredictionId != Context.PredictionId)
		{

			return false;
		}
	}

	UAnimSequenceBase* AuthoritativeAnimation = QuarantinedMove.IsSet()
		? QuarantinedMove->RootMotion.Animation.Get()
		: nullptr;
	FPP_ReactionDataEntry PredictedReaction;
	const UAnimSequenceBase* PredictedAnimation =
		ReactionData && ReactionData->FindReaction(PredictedReactionTag, PredictedReaction) &&
		PredictedReaction.Montage
			? (PredictedReaction.Montage->IsDynamicMontage()
				? PredictedReaction.Montage->GetFirstAnimReference()
				: PredictedReaction.Montage.Get())
			: nullptr;
	const bool bQuarantinedMoveMatches = QuarantinedMove.IsSet() &&
		QuarantinedMove->RootMotion.bIsActive && AuthoritativeAnimation &&
		AuthoritativeAnimation != PredictedAnimation;
	if (!bQuarantinedMoveMatches)
	{

		return false;
	}

	const float QuarantinedPosition = QuarantinedMove->RootMotion.Position;
	auto DoesMontageMatchAuthoritativeAnimation = [AuthoritativeAnimation](const UAnimMontage* Montage)
	{
		return Montage &&
			(Montage->IsDynamicMontage() ? Montage->GetFirstAnimReference() : Montage) ==
				AuthoritativeAnimation;
	};
	UGameplayAbility* AuthoritativeAbility = ASC->GetAnimatingAbility();
	UAnimMontage* AnimatingAbilityMontage = AuthoritativeAbility && AuthoritativeAbility->IsActive()
		? AuthoritativeAbility->GetCurrentMontage()
		: nullptr;
	UAnimMontage* AbilitySystemMontage = ASC->GetCurrentMontage();
	const FRepRootMotionMontage& CurrentRepRootMotion = TargetCharacter->GetRepRootMotion();
	const bool bMatchingAnimatingAbility =
		DoesMontageMatchAuthoritativeAnimation(AnimatingAbilityMontage);
	const bool bMatchingAbilitySystemMontage =
		DoesMontageMatchAuthoritativeAnimation(AbilitySystemMontage);
	const bool bMatchingReplicatedRootMotion = CurrentRepRootMotion.bIsActive &&
		CurrentRepRootMotion.Animation.Get() == AuthoritativeAnimation;
	if (!bMatchingAnimatingAbility && !bMatchingAbilitySystemMontage &&
		!bMatchingReplicatedRootMotion)
	{

		return false;
	}

	UAnimMontage* MontageToResume = AnimatingAbilityMontage;
	const TCHAR* RestoreSource = TEXT("AnimatingAbility");
	if (!DoesMontageMatchAuthoritativeAnimation(MontageToResume))
	{
		MontageToResume = AbilitySystemMontage;
		RestoreSource = TEXT("AbilitySystemCurrentMontage");
	}
	if (!DoesMontageMatchAuthoritativeAnimation(MontageToResume) &&
		bMatchingReplicatedRootMotion)
	{
		MontageToResume = Cast<UAnimMontage>(CurrentRepRootMotion.Animation.Get());
		RestoreSource = TEXT("CurrentReplicatedRootMotionMontage");
	}
	if (!DoesMontageMatchAuthoritativeAnimation(MontageToResume))
	{
		// Root-motion replication carries the actual montage for non-dynamic montages. This is
		// the normal fallback for simulated proxies because GAS does not retain an animating
		// ability pointer for them after a client-only predicted reaction displaces the montage.
		MontageToResume = Cast<UAnimMontage>(AuthoritativeAnimation);
		RestoreSource = TEXT("QuarantinedRootMotionMontage");
	}

	if (!DoesMontageMatchAuthoritativeAnimation(MontageToResume))
	{

		return false;
	}

	float PlayRate = ObservedPlayRate;
	if (!FMath::IsFinite(PlayRate) || FMath::Abs(PlayRate) < 0.1f || FMath::Abs(PlayRate) > 5.0f)
	{
		PlayRate = AnimInstance->Montage_GetPlayRate(MontageToResume);
	}
	if (FMath::Abs(PlayRate) <= KINDA_SMALL_NUMBER)
	{
		PlayRate = 1.0f;
	}

	const float MoveAgeSeconds = GetWorld()
		? static_cast<float>(FMath::Clamp(
			GetWorld()->GetTimeSeconds() - QuarantinedMove->Time, 0.0, 0.5))
		: 0.0f;
	const float AuthoritativePosition = QuarantinedPosition + (MoveAgeSeconds * PlayRate);
	const float ClampedPosition = FMath::Clamp(
		AuthoritativePosition, 0.0f,
		FMath::Max(0.0f, MontageToResume->GetPlayLength() - KINDA_SMALL_NUMBER));
	const float RemainingTrackDistance = PlayRate >= 0.0f
		? FMath::Max(0.0f, MontageToResume->GetPlayLength() - ClampedPosition)
		: ClampedPosition;
	const float RemainingMontageSeconds =
		RemainingTrackDistance / FMath::Max(FMath::Abs(PlayRate), KINDA_SMALL_NUMBER);
	const float EndGuardSeconds = FMath::Max(0.0f, RejectedPredictionMontageRestoreEndGuardSeconds);
	if (RemainingMontageSeconds <= EndGuardSeconds)
	{

		return false;
	}

	if (ASC->PlayMontageSimulated(MontageToResume, PlayRate) <= 0.0f)
	{

		return false;
	}

	// Jump directly to the replicated track position. Unlike GAS's ordinary OnRep fast-forward,
	// this intentionally does not replay montage notifies from the skipped prediction window.
	AnimInstance->Montage_SetPosition(MontageToResume, ClampedPosition);
	AnimInstance->Montage_SetPlayRate(MontageToResume, PlayRate);


	return true;
}

void UPP_PredictionComponent::RecordAuthoritativeCollision(AActor* TargetActor, FGameplayTag ReactionTag,
	const FPP_ReactionTransformSettings& TransformSettings, const FPP_ReactionDefenseSettings& DefenseSettings, 
	const FPP_ReactionDamageSettings& DamageSettings, const FPP_ReactionGameplayCueSettings& GameplayCueSettings,
	const FHitResult& HitResult)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !World || !TargetActor ||
		TargetActor == OwnerActor || !ReactionTag.IsValid())
	{
		return;
	}

	// A listen-server, standalone owner, NPC, or non-pawn attacker has no remote predicting
	// client request to match. Only a remote player-controlled pawn waits for the client RPC.
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	const bool bHasRemotePredictingOwner = OwnerPawn &&
		OwnerPawn->IsPlayerControlled() && !OwnerPawn->IsLocallyControlled();
	if (!bHasRemotePredictingOwner)
	{
		const FPP_ReactionPredictionContext Context = MakeReactionPredictionContext(TargetActor);
		DeferServerConfirmedReactionFromCollision(Context, TargetActor, ReactionTag,
			TransformSettings, DefenseSettings, DamageSettings, GameplayCueSettings, HitResult);
		return;
	}

	RemoveExpiredAuthoritativeCollisionState();

	for (int32 Index = 0; Index < PendingServerReactionRequests.Num(); ++Index)
	{
		const FPP_PendingServerReactionRequest& Request = PendingServerReactionRequests[Index];
		if (Request.TargetActor.Get() != TargetActor || Request.ReactionTag != ReactionTag) continue;

		const FPP_ReactionPredictionContext Context = Request.Context;
		PendingServerReactionRequests.RemoveAt(Index, 1, EAllowShrinking::No);
		DeferServerConfirmedReactionFromCollision(Context, TargetActor, ReactionTag,
			TransformSettings, DefenseSettings, DamageSettings, GameplayCueSettings, HitResult);
		return;
	}

	for (FPP_AuthoritativeCollisionRecord& ExistingRecord : AuthoritativeCollisionRecords)
	{
		if (ExistingRecord.TargetActor.Get() != TargetActor || ExistingRecord.ReactionTag != ReactionTag) continue;

		ExistingRecord.TransformSettings = TransformSettings;
		ExistingRecord.DefenseSettings = DefenseSettings;
		ExistingRecord.DamageSettings = DamageSettings;
		ExistingRecord.GameplayCueSettings = GameplayCueSettings;
		ExistingRecord.HitResult = HitResult;
		ExistingRecord.TimeSeconds = World->GetTimeSeconds();
		return;
	}

	if (AuthoritativeCollisionRecords.Num() >= MaxAuthoritativeCollisionRecords)
	{
		AuthoritativeCollisionRecords.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FPP_AuthoritativeCollisionRecord& Record = AuthoritativeCollisionRecords.AddDefaulted_GetRef();
	Record.TargetActor = TargetActor;
	Record.ReactionTag = ReactionTag;
	Record.TransformSettings = TransformSettings;
	Record.DefenseSettings = DefenseSettings;
	Record.DamageSettings = DamageSettings;
	Record.GameplayCueSettings = GameplayCueSettings;
	Record.HitResult = HitResult;
	Record.TimeSeconds = World->GetTimeSeconds();
}

void UPP_PredictionComponent::RecordAuthoritativeCollisionSweep(
	FGameplayTag ReactionTag,
	const FTransform& TraceStartTransform,
	const FTransform& TraceEndTransform,
	const FCollisionShape& CollisionShape,
	ECollisionChannel TraceChannel,
	const FPP_ReactionTransformSettings& TransformSettings,
	const FPP_ReactionDefenseSettings& DefenseSettings,
	const FPP_ReactionDamageSettings& DamageSettings,
	const FPP_ReactionGameplayCueSettings& GameplayCueSettings)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	const bool bHasRemotePredictingOwner = OwnerPawn && OwnerPawn->IsPlayerControlled() &&
		!OwnerPawn->IsLocallyControlled();
	if (!bEnableServerHitLagCompensation || !bRequireAuthoritativeCollisionMatch ||
		!OwnerActor || !OwnerActor->HasAuthority() || !World || !bHasRemotePredictingOwner ||
		!ReactionTag.IsValid() || CollisionShape.IsNearlyZero())
	{
		return;
	}

	RemoveExpiredAuthoritativeCollisionState();
	if (AuthoritativeCollisionSweepRecords.Num() >= MaxAuthoritativeCollisionSweepRecords)
	{
		AuthoritativeCollisionSweepRecords.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FPP_AuthoritativeCollisionSweepRecord& Record =
		AuthoritativeCollisionSweepRecords.AddDefaulted_GetRef();
	Record.ReactionTag = ReactionTag;
	Record.TraceStartTransform = TraceStartTransform;
	Record.TraceEndTransform = TraceEndTransform;
	Record.AttackerTransform = OwnerActor->GetActorTransform();
	Record.CollisionShape = CollisionShape;
	Record.TraceChannel = TraceChannel;
	Record.TransformSettings = TransformSettings;
	Record.DefenseSettings = DefenseSettings;
	Record.DamageSettings = DamageSettings;
	Record.GameplayCueSettings = GameplayCueSettings;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActor(OwnerActor))
	{
		UGameplayAbility* AnimatingAbility = ASC->GetAnimatingAbility();
		UAnimMontage* AnimatingMontage = AnimatingAbility
			? AnimatingAbility->GetCurrentMontage() : nullptr;
		Record.AnimatingAbility = AnimatingAbility;
		Record.AnimatingMontage = AnimatingMontage;
		if (const ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerActor))
		{
			if (const UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()
				? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr)
			{
				Record.MontagePosition = AnimatingMontage
					? AnimInstance->Montage_GetPosition(AnimatingMontage) : -1.0f;
			}
		}
	}
	Record.TimeSeconds = World->GetTimeSeconds();

	ResolvePendingHistoricalCollisionRequests(Record);
}

void UPP_PredictionComponent::ResolvePendingHistoricalCollisionRequests(
	FPP_AuthoritativeCollisionSweepRecord& SweepRecord)
{
	for (int32 Index = PendingServerReactionRequests.Num() - 1; Index >= 0; --Index)
	{
		FPP_PendingServerReactionRequest& Request = PendingServerReactionRequests[Index];
		AActor* TargetActor = Request.TargetActor.Get();
		if (!TargetActor || Request.ReactionTag != SweepRecord.ReactionTag ||
			SweepRecord.ValidatedTargets.Contains(TWeakObjectPtr<AActor>(TargetActor)))
		{
			continue;
		}

		FHitResult HistoricalHitResult;
		const TCHAR* FailureReason = TEXT("Unknown");
		if (!TryValidateHistoricalCollisionSweep(
			Request.Context, TargetActor, SweepRecord, HistoricalHitResult, FailureReason))
		{
			++Request.HistoricalSweepCandidatesTested;
			Request.LastHistoricalSweepFailureReason = FName(FailureReason);
			continue;
		}
		++Request.HistoricalSweepCandidatesTested;

		const FPP_ReactionPredictionContext Context = Request.Context;
		const FGameplayTag ReactionTag = Request.ReactionTag;
		SweepRecord.ValidatedTargets.Add(TargetActor);
		PendingServerReactionRequests.RemoveAt(Index, 1, EAllowShrinking::No);

		DeferServerConfirmedReactionFromCollision(
			Context, TargetActor, ReactionTag,
			SweepRecord.TransformSettings, SweepRecord.DefenseSettings,
			SweepRecord.DamageSettings, SweepRecord.GameplayCueSettings,
			HistoricalHitResult);
	}
}

void UPP_PredictionComponent::DeferServerConfirmedReactionFromCollision(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	const FPP_ReactionTransformSettings& TransformSettings,
	const FPP_ReactionDefenseSettings& DefenseSettings,
	const FPP_ReactionDamageSettings& DamageSettings,
	const FPP_ReactionGameplayCueSettings& GameplayCueSettings,
	const FHitResult& HitResult)
{
	UWorld* World = GetWorld();
	if (!World || !Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	// Collision notifies run during an actor's animation tick. Starting root motion on another
	// character here is tick-order dependent: if that target already moved this frame, its mesh can
	// advance the montage once before Character Movement can consume the first root-motion slice.
	// Queue until the following frame and make target movement depend on this component for that
	// frame. Request-after-collision processing remains immediate and does not use this queue.
	for (FPP_DeferredServerReactionFromCollision& Existing : DeferredServerReactionsFromCollision)
	{
		if (Existing.Context.PredictionId != Context.PredictionId) continue;

		Existing.TargetActor = TargetActor;
		Existing.ReactionTag = ReactionTag;
		Existing.TransformSettings = TransformSettings;
		Existing.DefenseSettings = DefenseSettings;
		Existing.DamageSettings = DamageSettings;
		Existing.GameplayCueSettings = GameplayCueSettings;
		Existing.HitResult = HitResult;
		return;
	}

	FPP_DeferredServerReactionFromCollision& State = DeferredServerReactionsFromCollision.AddDefaulted_GetRef();
	State.Context = Context;
	State.TargetActor = TargetActor;
	State.ReactionTag = ReactionTag;
	State.TransformSettings = TransformSettings;
	State.DefenseSettings = DefenseSettings;
	State.DamageSettings = DamageSettings;
	State.GameplayCueSettings = GameplayCueSettings;
	State.HitResult = HitResult;
	State.EarliestFrameCounter = GFrameCounter + 1;

	if (ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		State.MovementComponent = TargetCharacter->GetCharacterMovement();
		if (UCharacterMovementComponent* MovementComponent = State.MovementComponent.Get())
		{
			MovementComponent->AddTickPrerequisiteComponent(this);
		}
	}

	SetComponentTickEnabled(true);
}

void UPP_PredictionComponent::ProcessDeferredServerReactionsFromCollision()
{
	for (int32 Index = DeferredServerReactionsFromCollision.Num() - 1; Index >= 0; --Index)
	{
		if (GFrameCounter < DeferredServerReactionsFromCollision[Index].EarliestFrameCounter) continue;

		const FPP_DeferredServerReactionFromCollision State =
			DeferredServerReactionsFromCollision[Index];
		DeferredServerReactionsFromCollision.RemoveAtSwap(Index, 1, EAllowShrinking::No);

		UCharacterMovementComponent* MovementComponent = State.MovementComponent.Get();
		const bool bMovementStillRequired = MovementComponent &&
			DeferredServerReactionsFromCollision.ContainsByPredicate(
				[MovementComponent](const FPP_DeferredServerReactionFromCollision& Other)
				{
					return Other.MovementComponent.Get() == MovementComponent;
				});
		if (MovementComponent && !bMovementStillRequired)
		{
			MovementComponent->RemoveTickPrerequisiteComponent(this);
		}

		AActor* TargetActor = State.TargetActor.Get();
		if (!TargetActor) continue;

		if (!ProcessServerConfirmedReaction(
			State.Context, TargetActor, State.ReactionTag,
			State.TransformSettings, State.DefenseSettings, State.DamageSettings,
			State.GameplayCueSettings, State.HitResult))
		{
			ClientRejectPredictedReaction(
				State.Context, TargetActor, State.ReactionTag,
				TargetActor->GetActorLocation(), TargetActor->GetActorRotation());
		}
	}
}

void UPP_PredictionComponent::ScheduleServerRootMotionStartAlignment(const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag, UAnimMontage* Montage, float StartPosition,
	const FVector& ServerStartLocation, int32 Attempt)
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !Context.IsValid() ||
		!TargetActor || !Montage || Montage->IsValidAdditive() || !ReactionTag.IsValid())
	{
		return;
	}

	const TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
	const TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	const TWeakObjectPtr<UAnimMontage> WeakMontage(Montage);
	const FPP_ReactionPredictionContext CapturedContext = Context;
	const FGameplayTag CapturedReactionTag = ReactionTag;

	// Sample after animation/movement have had a chance to advance the montage. Depending on actor
	// tick order, the first next-tick callback can still see the montage at StartPosition, so retry a
	// few frames instead of treating "not advanced yet" as proof that no alignment is needed.
	World->GetTimerManager().SetTimerForNextTick(
		[WeakThis, WeakTarget, WeakMontage, CapturedContext, CapturedReactionTag,
			StartPosition, ServerStartLocation, Attempt]()
		{
			UPP_PredictionComponent* StrongThis = WeakThis.Get();
			ACharacter* StrongTarget = Cast<ACharacter>(WeakTarget.Get());
			UAnimMontage* StrongMontage = WeakMontage.Get();
			if (!StrongThis || !StrongTarget || !StrongMontage) return;

			USkeletalMeshComponent* Mesh = StrongTarget->GetMesh();
			UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
			UCharacterMovementComponent* MovementComponent = StrongTarget->GetCharacterMovement();
			if (!AnimInstance || !MovementComponent) return;

			const float CurrentPosition = AnimInstance->Montage_GetPosition(StrongMontage);
			if (CurrentPosition <= StartPosition + KINDA_SMALL_NUMBER)
			{
				if (Attempt < 3 && AnimInstance->Montage_IsPlaying(StrongMontage))
				{
					StrongThis->ScheduleServerRootMotionStartAlignment(
						CapturedContext, StrongTarget, CapturedReactionTag, StrongMontage,
						StartPosition, ServerStartLocation, Attempt + 1);
				}
				return;
			}

			// Build the position the authoritative character should have reached from the montage curve,
			// measured from the location captured immediately before montage playback. A collision notify
			// can start another actor's montage at an awkward point in the frame; in that case animation
			// advances but Character Movement misses that initial root-motion slice. This comparison finds
			// only that missing start displacement and does not continuously steer the reaction.
			const FTransform LocalRootMotion = StrongMontage->ExtractRootMotionFromTrackRange(
				StartPosition, CurrentPosition, FAnimExtractContext());
			const FTransform WorldRootMotion = MovementComponent->ConvertLocalRootMotionToWorld(
				LocalRootMotion, StrongTarget->GetWorld() ? StrongTarget->GetWorld()->GetDeltaSeconds() : 0.0f);

			const FVector ActualLocation = StrongTarget->GetActorLocation();
			FVector ExpectedLocation = ServerStartLocation + WorldRootMotion.GetTranslation();
			// Walking Character Movement intentionally discards montage Z translation, so matching Z here
			// would manufacture a vertical correction that normal montage movement never applied.
			ExpectedLocation.Z = ActualLocation.Z;
			const float MissingDistance = FVector::Dist2D(ActualLocation, ExpectedLocation);
			const float MinDistance = FMath::Max(0.0f, StrongThis->ServerRootMotionStartAlignmentMinDistance);
			const float MaxDistance = FMath::Max(MinDistance, StrongThis->ServerRootMotionStartAlignmentMaxDistance);
			// Leave already-close starts alone (the normal replication path handles their small error), and
			// reject unusually large differences because they are more likely stale/bad state than one lost
			// root-motion slice. The accepted correction is swept below so world collision is still respected.
			const bool bApplyAlignment = MissingDistance > MinDistance && MissingDistance <= MaxDistance;

			if (!bApplyAlignment) return;

			// This is an authoritative alignment, not a teleport through geometry. A blocking sweep may stop
			// short of ExpectedLocation.
			StrongTarget->SetActorLocation(ExpectedLocation, true, nullptr, ETeleportType::None);
		});
}

void UPP_PredictionComponent::RemoveExpiredAuthoritativeCollisionState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		AuthoritativeCollisionRecords.Reset();
		AuthoritativeCollisionSweepRecords.Reset();
		PendingServerReactionRequests.Reset();
		return;
	}

	const double NowSeconds = World->GetTimeSeconds();
	const double RecordLifetime = FMath::Max(0.1f, AuthoritativeCollisionRecordLifetime);

	for (int32 Index = AuthoritativeCollisionRecords.Num() - 1; Index >= 0; --Index)
	{
		const FPP_AuthoritativeCollisionRecord& Record = AuthoritativeCollisionRecords[Index];
		if (!Record.TargetActor.IsValid() || NowSeconds - Record.TimeSeconds > RecordLifetime)
		{
			AuthoritativeCollisionRecords.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	for (int32 Index = AuthoritativeCollisionSweepRecords.Num() - 1; Index >= 0; --Index)
	{
		const FPP_AuthoritativeCollisionSweepRecord& Record =
			AuthoritativeCollisionSweepRecords[Index];
		if (!Record.ReactionTag.IsValid() || NowSeconds - Record.TimeSeconds > RecordLifetime)
		{
			AuthoritativeCollisionSweepRecords.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	for (int32 Index = PendingServerReactionRequests.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PendingServerReactionRequest& Request = PendingServerReactionRequests[Index];
		// Valid pending requests are removed by their rejection timer so the client always receives a result.
		if (!Request.TargetActor.IsValid())
		{
			PendingServerReactionRequests.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

bool UPP_PredictionComponent::ConsumeAuthoritativeCollision(
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	FPP_ReactionTransformSettings& OutTransformSettings,
	FPP_ReactionDefenseSettings& OutDefenseSettings,
	FPP_ReactionDamageSettings& OutDamageSettings,
	FPP_ReactionGameplayCueSettings& OutGameplayCueSettings,
	FHitResult& OutHitResult)
{
	RemoveExpiredAuthoritativeCollisionState();

	for (int32 Index = 0; Index < AuthoritativeCollisionRecords.Num(); ++Index)
	{
		const FPP_AuthoritativeCollisionRecord& Record = AuthoritativeCollisionRecords[Index];
		if (Record.TargetActor.Get() != TargetActor || Record.ReactionTag != ReactionTag) continue;

		OutTransformSettings = Record.TransformSettings;
		OutDefenseSettings = Record.DefenseSettings;
		OutDamageSettings = Record.DamageSettings;
		OutGameplayCueSettings = Record.GameplayCueSettings;
		OutHitResult = Record.HitResult;
		AuthoritativeCollisionRecords.RemoveAt(Index, 1, EAllowShrinking::No);
		return true;
	}

	return false;
}

void UPP_PredictionComponent::QueuePendingServerReactionRequest(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag)
{
	UWorld* World = GetWorld();
	if (!World || !Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	RemoveExpiredAuthoritativeCollisionState();

	for (FPP_PendingServerReactionRequest& ExistingRequest : PendingServerReactionRequests)
	{
		if (ExistingRequest.Context.PredictionId != Context.PredictionId) continue;

		// A reliable RPC should not duplicate, but do not extend the validation window if it does.
		return;
	}

	if (PendingServerReactionRequests.Num() >= MaxPendingServerReactionRequests)
	{
		const FPP_PendingServerReactionRequest EvictedRequest = PendingServerReactionRequests[0];
		PendingServerReactionRequests.RemoveAt(0, 1, EAllowShrinking::No);
		if (AActor* EvictedTarget = EvictedRequest.TargetActor.Get())
		{
			ClientRejectPredictedReaction(
				EvictedRequest.Context, EvictedTarget, EvictedRequest.ReactionTag,
				EvictedTarget->GetActorLocation(), EvictedTarget->GetActorRotation());
		}
	}

	FPP_PendingServerReactionRequest& Request = PendingServerReactionRequests.AddDefaulted_GetRef();
	Request.Context = Context;
	Request.TargetActor = TargetActor;
	Request.ReactionTag = ReactionTag;
	Request.TimeSeconds = World->GetTimeSeconds();

	const TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
	const TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	const FPP_ReactionPredictionContext CapturedContext = Context;
	const FGameplayTag CapturedReactionTag = ReactionTag;
	const float Timeout = FMath::Max(0.05f, AuthoritativeCollisionMatchTimeout);

	FTimerHandle RejectionTimer;
	World->GetTimerManager().SetTimer(RejectionTimer,
		[WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
		{
			UPP_PredictionComponent* StrongThis = WeakThis.Get();
			AActor* StrongTarget = WeakTarget.Get();
			if (!StrongThis || !StrongTarget) return;

			for (int32 Index = 0; Index < StrongThis->PendingServerReactionRequests.Num(); ++Index)
			{
				const FPP_PendingServerReactionRequest& Pending =
					StrongThis->PendingServerReactionRequests[Index];
				if (Pending.Context.PredictionId != CapturedContext.PredictionId ||
					Pending.TargetActor.Get() != StrongTarget || Pending.ReactionTag != CapturedReactionTag)
				{
					continue;
				}

				StrongThis->PendingServerReactionRequests.RemoveAt(Index, 1, EAllowShrinking::No);

				StrongThis->ClientRejectPredictedReaction(
					CapturedContext, StrongTarget, CapturedReactionTag,
					StrongTarget->GetActorLocation(), StrongTarget->GetActorRotation());
				return;
			}
		}, Timeout, false);
}

bool UPP_PredictionComponent::ConsumeServerReactionRequestBudget()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	const double NowSeconds = World->GetTimeSeconds();
	if (ServerReactionRequestWindowStartSeconds < 0.0 ||
		NowSeconds - ServerReactionRequestWindowStartSeconds >= 1.0)
	{
		ServerReactionRequestWindowStartSeconds = NowSeconds;
		ServerReactionRequestsInCurrentWindow = 0;

		for (auto It = LastServerReactionTimeByTarget.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	if (MaxServerReactionRequestsPerSecond > 0 &&
		ServerReactionRequestsInCurrentWindow >= MaxServerReactionRequestsPerSecond)
	{
		return false;
	}

	++ServerReactionRequestsInCurrentWindow;
	return true;
}

bool UPP_PredictionComponent::ValidateServerReactionRequest(
	AActor* TargetActor,
	const FPP_ReactionDataEntry& Reaction,
	const FPP_ReactionTransformSettings& TransformSettings)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !TargetActor || TargetActor == OwnerActor || !World ||
		TargetActor->GetWorld() != World || TargetActor->IsActorBeingDestroyed() ||
		!Cast<ACharacter>(TargetActor))
	{
		return false;
	}

	if (MaxServerReactionRequestDistance > 0.0f &&
		FVector::DistSquared(OwnerActor->GetActorLocation(), TargetActor->GetActorLocation()) >
		FMath::Square(MaxServerReactionRequestDistance))
	{
		return false;
	}

	if (TransformSettings.bUseServerStartTransform)
	{
		return false;
	}

	const FPP_ReactionMovementSettings& Movement = TransformSettings.MovementSettings;
	const FPP_ReactionRotationSettings& Rotation = TransformSettings.RotationSettings;

	const bool bEnumsValid =
		PP_IsValidReactionEnumValue(Movement.MoveDirection) &&
		PP_IsValidReactionEnumValue(Movement.Recipient) &&
		PP_IsValidReactionEnumValue(Movement.ReferenceActorSource) &&
		PP_IsValidReactionEnumValue(Movement.LateralOffsetMode) &&
		PP_IsValidReactionEnumValue(Movement.TeleportType) &&
		PP_IsValidReactionEnumValue(Rotation.RotationDirection) &&
		PP_IsValidReactionEnumValue(Rotation.Recipient) &&
		PP_IsValidReactionEnumValue(Rotation.ReferenceActorSource) &&
		PP_IsValidReactionEnumValue(Rotation.TeleportType);
	if (!bEnumsValid || !FMath::IsFinite(Movement.MoveDistance) || !FMath::IsFinite(Movement.LateralOffset) ||
		!FMath::IsFinite(Movement.ClientInterpolationSpeed) || Movement.MoveDistance < 0.0f ||
		Movement.ClientInterpolationSpeed < 0.0f ||
		(MaxServerReactionMoveDistance > 0.0f && Movement.MoveDistance > MaxServerReactionMoveDistance) ||
		(MaxServerReactionLateralOffset > 0.0f &&
			FMath::Abs(Movement.LateralOffset) > MaxServerReactionLateralOffset) ||
		Rotation.DirectionToFace.ContainsNaN() || Rotation.ReferenceFacingOverride.ContainsNaN())
	{
		return false;
	}

	const double NowSeconds = World->GetTimeSeconds();
	if (const double* LastAcceptedTime = LastServerReactionTimeByTarget.Find(TargetActor))
	{
		if (NowSeconds - *LastAcceptedTime < FMath::Max(0.0f, Reaction.MinReplayInterval))
		{
			return false;
		}
	}

	LastServerReactionTimeByTarget.Add(TargetActor, NowSeconds);
	return true;
}

bool UPP_PredictionComponent::CanServerAcceptPredictedReaction_Implementation(
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	const FPP_ReactionTransformSettings& TransformSettings) const
{
	return TargetActor != nullptr && ReactionTag.IsValid();
}

EPP_ReactionDefenseOutcome UPP_PredictionComponent::ResolveDefenseOutcome(
	AActor* TargetActor,
	const FPP_ReactionDefenseSettings& DefenseSettings) const
{
	const UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	if (!TargetCombat) return EPP_ReactionDefenseOutcome::None;

	const bool bBlocked = DefenseSettings.Block.bBlockable &&
		TargetCombat->CanBlockAttackFrom(GetOwner(), DefenseSettings.Block.BlockAngleDegrees);
	if (bBlocked && TargetCombat->IsParryingActive()) return EPP_ReactionDefenseOutcome::Parried;
	if (DefenseSettings.Dodge.bDodgeable && TargetCombat->IsDodgingActive()) return EPP_ReactionDefenseOutcome::Dodged;
	if (bBlocked) return EPP_ReactionDefenseOutcome::Blocked;
	if (DefenseSettings.SuperArmor.RequiredSuperArmor != EPP_SuperArmorLevel::None &&
		TargetCombat->GetSuperArmorLevel() >= DefenseSettings.SuperArmor.RequiredSuperArmor)
	{
		return EPP_ReactionDefenseOutcome::SuperArmored;
	}

	return EPP_ReactionDefenseOutcome::None;
}

bool UPP_PredictionComponent::ShouldApplyReactionTransform(
	const EPP_ReactionDefenseOutcome Outcome,
	const FPP_ReactionDefenseSettings& DefenseSettings)
{
	if (Outcome == EPP_ReactionDefenseOutcome::None) return true;
	if (Outcome == EPP_ReactionDefenseOutcome::Blocked || Outcome == EPP_ReactionDefenseOutcome::Parried)
	{
		return DefenseSettings.Block.bAllowMovementWhenBlocked ||
			DefenseSettings.Block.bAllowRotationWhenBlocked;
	}
	return false;
}

void UPP_PredictionComponent::ApplyDefenseOutcomeEffects(
	AActor* TargetActor,
	const EPP_ReactionDefenseOutcome Outcome) const
{
	UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	if (!TargetCombat) return;

	switch (Outcome)
	{
	case EPP_ReactionDefenseOutcome::Parried:
		// Parry is a specialized successful block, so both configured responses fire.
		TargetCombat->ApplySuccessfulBlockEffects(GetOwner());
		TargetCombat->ApplySuccessfulParryEffects(GetOwner());
		break;
	case EPP_ReactionDefenseOutcome::Blocked:
		TargetCombat->ApplySuccessfulBlockEffects(GetOwner());
		break;
	case EPP_ReactionDefenseOutcome::Dodged:
		TargetCombat->ApplySuccessfulDodgeEffects(GetOwner());
		break;
	case EPP_ReactionDefenseOutcome::SuperArmored:
		TargetCombat->ApplySuccessfulSuperArmorEffects(GetOwner());
		break;
	default:
		break;
	}
}

void UPP_PredictionComponent::ShowDefenseOutcomeCombatTextToAttacker(
	AActor* TargetActor,
	const EPP_ReactionDefenseOutcome Outcome,
	const FHitResult& HitResult) const
{
	EPP_CombatTextType CombatTextType;
	switch (Outcome)
	{
	case EPP_ReactionDefenseOutcome::Blocked:
	case EPP_ReactionDefenseOutcome::Parried:
		CombatTextType = EPP_CombatTextType::AttackBlocked;
		break;
	case EPP_ReactionDefenseOutcome::Dodged:
		CombatTextType = EPP_CombatTextType::AttackDodged;
		break;
	case EPP_ReactionDefenseOutcome::SuperArmored:
		CombatTextType = EPP_CombatTextType::AttackSuperArmored;
		break;
	default:
		return;
	}

	AActor* AttackerActor = GetOwner();
	UPP_CombatTextComponent* CombatText =
		AttackerActor ? AttackerActor->FindComponentByClass<UPP_CombatTextComponent>() : nullptr;
	if (!CombatText) return;

	FVector WorldLocation = HitResult.ImpactPoint;
	if (!HitResult.bBlockingHit || WorldLocation.ContainsNaN())
	{
		if (!TargetActor) return;

		FVector Origin;
		FVector Extent;
		TargetActor->GetActorBounds(true, Origin, Extent);
		WorldLocation = Origin + FVector(0.0f, 0.0f, Extent.Z);
	}

	CombatText->ShowCombatTextToOwner(0.0f, WorldLocation, CombatTextType);
}

bool UPP_PredictionComponent::ShouldApplyDamage(
	AActor* TargetActor,
	const EPP_ReactionDefenseOutcome Outcome,
	const FPP_ReactionDamageSettings& DamageSettings) const
{
	if (DamageSettings.DamageEffects.IsEmpty()) return false;
	if (Outcome == EPP_ReactionDefenseOutcome::Parried &&
		(!DamageSettings.bApplyDamageWhenParried || !DamageSettings.bApplyDamageWhenBlocked)) return false;
	if (Outcome == EPP_ReactionDefenseOutcome::Blocked && !DamageSettings.bApplyDamageWhenBlocked) return false;
	if (Outcome == EPP_ReactionDefenseOutcome::Dodged && !DamageSettings.bApplyDamageWhenDodged) return false;

	const UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	return !TargetCombat || !TargetCombat->DoesCurrentSuperArmorIgnoreDamage();
}

void UPP_PredictionComponent::ApplyDamageEffects(
	AActor* TargetActor,
	const FPP_ReactionDamageSettings& DamageSettings,
	const FHitResult& HitResult) const
{
	AActor* InstigatorActor = GetOwner();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActor(InstigatorActor);
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	if (!InstigatorActor || !InstigatorActor->HasAuthority() || !SourceASC || !TargetASC) return;

	for (const FPP_ReactionDamageEffect& DamageEffect : DamageSettings.DamageEffects)
	{
		if (!DamageEffect.GameplayEffectClass) continue;

		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(InstigatorActor, InstigatorActor);
		Context.AddSourceObject(this);
		Context.AddHitResult(HitResult, true);

		const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			DamageEffect.GameplayEffectClass, DamageEffect.EffectLevel, Context);
		if (Spec.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(
				TAG_Damage_Type_Physical, DamageEffect.PhysicalAttackMultiplier);
			Spec.Data->SetSetByCallerMagnitude(
				TAG_Damage_Type_Magical, DamageEffect.MagicalAttackMultiplier);
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		}
	}
}

void UPP_PredictionComponent::ExecuteActivationGameplayCues(
	const FPP_ReactionGameplayCueSettings& GameplayCueSettings) const
{
	ExecuteGameplayCuesLocally(
		nullptr, GameplayCueSettings, FHitResult(),
		EPP_ReactionGameplayCueTriggerTiming::OnActivation,
		EPP_GameplayCueExecutionFilter::All);
}

bool UPP_PredictionComponent::ExecuteGameplayCuesLocally(
	AActor* TargetActor,
	const FPP_ReactionGameplayCueSettings& GameplayCueSettings,
	const FHitResult& HitResult,
	const EPP_ReactionGameplayCueTriggerTiming TriggerTiming,
	const EPP_GameplayCueExecutionFilter ExecutionFilter) const
{
	if (GetNetMode() == NM_DedicatedServer || GameplayCueSettings.Cues.IsEmpty()) return false;

	AActor* InstigatorActor = GetOwner();
	UAbilitySystemComponent* InstigatorASC = GetAbilitySystemComponentFromActor(InstigatorActor);
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	if (!InstigatorActor || !InstigatorASC) return false;

	bool bExecutedAnyCue = false;
	for (const FPP_ReactionGameplayCue& Cue : GameplayCueSettings.Cues)
	{
		if (!Cue.CueTag.IsValid() || Cue.TriggerTiming != TriggerTiming) continue;
		if (ExecutionFilter == EPP_GameplayCueExecutionFilter::PredictableOnly && !Cue.bPredictLocally) continue;
		if (ExecutionFilter == EPP_GameplayCueExecutionFilter::ConfirmationOnly && Cue.bPredictLocally) continue;

		// Camera shake cues belong only to the locally controlled attacker. Invoking the parent cue
		// through the instigator ASC also preserves the behavior of the ProjectLogos hit-window code.
		if (Cue.CueTag.MatchesTag(TAG_GameplayCue_Hit_CameraShake))
		{
			const APawn* InstigatorPawn = Cast<APawn>(InstigatorActor);
			if (!InstigatorPawn || !InstigatorPawn->IsLocallyControlled()) continue;

			ExecuteGameplayCueOnASC(InstigatorASC, TargetASC, TargetActor, Cue, HitResult);
			bExecutedAnyCue = true;
			continue;
		}

		switch (Cue.Recipient)
		{
		case EPP_ReactionGameplayCueRecipient::Instigator:
			ExecuteGameplayCueOnASC(InstigatorASC, TargetASC, TargetActor, Cue, HitResult);
			bExecutedAnyCue = true;
			break;

		case EPP_ReactionGameplayCueRecipient::Target:
			if (TargetASC)
			{
				ExecuteGameplayCueOnASC(TargetASC, TargetASC, TargetActor, Cue, HitResult);
				bExecutedAnyCue = true;
			}
			break;

		case EPP_ReactionGameplayCueRecipient::Both:
			ExecuteGameplayCueOnASC(InstigatorASC, TargetASC, TargetActor, Cue, HitResult);
			bExecutedAnyCue = true;
			if (TargetASC && TargetASC != InstigatorASC)
			{
				ExecuteGameplayCueOnASC(TargetASC, TargetASC, TargetActor, Cue, HitResult);
			}
			break;

		default:
			break;
		}
	}

	return bExecutedAnyCue;
}

void UPP_PredictionComponent::ExecuteGameplayCueOnASC(
	UAbilitySystemComponent* ASC,
	UAbilitySystemComponent* TargetASC,
	AActor* TargetActor,
	const FPP_ReactionGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	if (!ASC || !Cue.CueTag.IsValid()) return;

	AActor* InstigatorActor = GetOwner();
	FGameplayCueParameters Params;
	Params.Instigator = InstigatorActor;
	Params.EffectCauser = InstigatorActor;
	Params.SourceObject = const_cast<UPP_PredictionComponent*>(this);
	Params.Location = GetGameplayCueSpawnLocation(Cue, HitResult);
	Params.Normal = HitResult.GetActor()
		? FVector(HitResult.ImpactNormal)
		: (InstigatorActor ? InstigatorActor->GetActorForwardVector() : FVector::ForwardVector);
	Params.TargetAttachComponent = GetGameplayCueAttachComponent(
		ASC, TargetASC, TargetActor, Cue, HitResult);

	// This function is called on each intended machine. Avoid ASC network dispatch here so the
	// custom reaction PredictionId remains the single source of duplicate suppression.
	ASC->InvokeGameplayCueEvent(Cue.CueTag, EGameplayCueEvent::Executed, Params);
}

FVector UPP_PredictionComponent::GetGameplayCueSpawnLocation(
	const FPP_ReactionGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	AActor* InstigatorActor = GetOwner();
	FVector SpawnLocation = InstigatorActor
		? InstigatorActor->GetActorLocation()
		: FVector::ZeroVector;

	if (HitResult.GetActor())
	{
		switch (Cue.SpawnPoint)
		{
		case EPP_ReactionGameplayCueSpawnPoint::InstigatorLocation:
			break;
		case EPP_ReactionGameplayCueSpawnPoint::HitImpactPoint:
			SpawnLocation = HitResult.ImpactPoint;
			break;
		case EPP_ReactionGameplayCueSpawnPoint::HitLocation:
			SpawnLocation = HitResult.Location;
			break;
		default:
			break;
		}
	}

	return SpawnLocation + Cue.LocationOffset;
}

USceneComponent* UPP_PredictionComponent::GetGameplayCueAttachComponent(
	UAbilitySystemComponent* ASC,
	UAbilitySystemComponent* TargetASC,
	AActor* TargetActor,
	const FPP_ReactionGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	if (!Cue.bAttachToTarget) return nullptr;

	if (ASC == TargetASC && TargetActor)
	{
		return HitResult.GetComponent()
			? HitResult.GetComponent()
			: TargetActor->GetRootComponent();
	}

	AActor* InstigatorActor = GetOwner();
	return InstigatorActor ? InstigatorActor->GetRootComponent() : nullptr;
}

bool UPP_PredictionComponent::PlayPredictedReactionOnTargetProxy
(AActor* TargetActor, FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings,
 FPP_ReactionDefenseSettings DefenseSettings, FPP_ReactionDamageSettings DamageSettings,
 FPP_ReactionGameplayCueSettings GameplayCueSettings, FHitResult HitResult)
{
	// Local prediction: validate -> transform/cosmetics -> mark -> server RPC.
	if (!ReactionData || !ReactionTag.IsValid()) return false;

	const FPP_ReactionTransformSettings RequestedTransformSettings = TransformSettings;
	const EPP_ReactionDefenseOutcome PredictedDefenseOutcome =
		ResolveDefenseOutcome(TargetActor, DefenseSettings);
	const FGameplayTag RequestedPlaybackReactionTag =
		PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::Blocked
			? TAG_Trigger_Hit_Block_Success
			: ReactionTag;

	FGameplayTag ResolvedReactionTag;
	FPP_ReactionDataEntry Reaction;
	bool bPreserveOriginalTransform = true;
	if (!ResolveReactionForTarget(
		TargetActor, RequestedPlaybackReactionTag, ResolvedReactionTag, Reaction,
		bPreserveOriginalTransform))
	{
		return false;
	}
	if (ResolvedReactionTag != RequestedPlaybackReactionTag && !bPreserveOriginalTransform)
	{
		SuppressReactionTransform(TransformSettings);
	}

	if (PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::Blocked ||
		PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::Parried)
	{
		// A successful guard normally keeps both characters in place unless this attack opts in.
		if (!DefenseSettings.Block.bAllowMovementWhenBlocked)
		{
			TransformSettings.MovementSettings.MoveDirection = EPP_ReactionMoveDirection::None;
		}
		if (!DefenseSettings.Block.bAllowRotationWhenBlocked)
		{
			TransformSettings.RotationSettings.RotationDirection = EPP_ReactionRotationDirection::None;
		}
	}

	if (!CanPlayPredictedReactionOnTargetProxy(TargetActor, Reaction)) return false;

	const FPP_ReactionPredictionContext Context = MakeReactionPredictionContext(TargetActor);
	const bool bPredictCleanReaction = PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::None;
	const bool bPredictBlockedAdditiveReaction =
		PredictedDefenseOutcome == EPP_ReactionDefenseOutcome::Blocked &&
		PP_IsAdditiveReaction(Reaction);
	const bool bPlayReactionLocally = bPredictCleanReaction || bPredictBlockedAdditiveReaction;
	const float StartPosition = GetReactionStartPosition(Reaction);
	UGameplayAbility* LocalAnimatingAbility = nullptr;
	UAnimMontage* LocalAnimatingMontage = nullptr;
	float LocalMontagePosition = -1.0f;
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActor(GetOwner()))
	{
		LocalAnimatingAbility = OwnerASC->GetAnimatingAbility();
		LocalAnimatingMontage = LocalAnimatingAbility
			? LocalAnimatingAbility->GetCurrentMontage() : nullptr;
		if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
		{
			if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()
				? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr)
			{
				LocalMontagePosition = LocalAnimatingMontage
					? AnimInstance->Montage_GetPosition(LocalAnimatingMontage) : -1.0f;
			}
		}
	}


	int32 PredictedProxyAnimTagHandle = INDEX_NONE;
	bool bPlayedGameplayCuesLocally = false;

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		TransformSettings.RotationSettings.bUseReferenceFacingOverride = true;
		TransformSettings.RotationSettings.ReferenceFacingOverride =
			FRotator(0.f, OwnerPawn->GetControlRotation().Yaw, 0.f);
	}

	if (bPredictCleanReaction)
	{
		ApplyLatestOlderProxyPendingFinalReactionCorrection(Context, TargetActor, TEXT("PredictedPreflight"));
		ApplyReactionTransform(GetOwner(), TargetActor, TransformSettings);
	}

	if (bPlayReactionLocally)
	{
		if (Reaction.Montage &&
			!PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true))
		{
			return false;
		}

		// Protect the predicted proxy immediately. Waiting for the server round trip allows an
		// older replicated ability montage to overwrite this reaction under latency.
		if (Reaction.Montage && !PP_IsAdditiveReaction(Reaction))
		{
			BeginPredictedProxyRootMotionReconciliation(
				Context, TargetActor, ResolvedReactionTag, false);
		}
	}

	if (bPredictCleanReaction)
	{
		// Montage reactions already provide immediate feedback. A montage-less reaction instead
		// predicts only the animation-facing tags granted by its target effects.
		if (!Reaction.Montage)
		{
			PredictedProxyAnimTagHandle =
				BeginPredictedProxyTargetEffectFeedback(TargetActor, Reaction);
		}

		bPlayedGameplayCuesLocally = ExecuteGameplayCuesLocally(
			TargetActor, GameplayCueSettings, HitResult,
			EPP_ReactionGameplayCueTriggerTiming::OnHit,
			EPP_GameplayCueExecutionFilter::PredictableOnly);
	}

	AddPendingPredictedReaction(
		Context, TargetActor, ResolvedReactionTag, bPlayReactionLocally,
		bPlayedGameplayCuesLocally, PredictedProxyAnimTagHandle);

	if (const UWorld* World = GetWorld())
	{
		LastReactionTimeByTarget.Add(TargetActor, World->GetTimeSeconds());
	}

	// Send the clean-hit request; the server independently selects the defensive outcome.
	ServerConfirmPredictedReaction(Context, TargetActor, ReactionTag, RequestedTransformSettings,
		DefenseSettings, DamageSettings, GameplayCueSettings, HitResult);

	return true;
}

bool UPP_PredictionComponent::TryReservePredictedCollisionTarget(
	AActor* TargetActor,
	const FGameplayTag ReactionTag)
{
	if (!TargetActor || !ReactionTag.IsValid())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActor(GetOwner());
	UPP_GameplayAbility* AnimatingAbility = ASC
		? Cast<UPP_GameplayAbility>(ASC->GetAnimatingAbility()) : nullptr;
	if (!AnimatingAbility)
	{
		// Non-project abilities keep the notify-window duplicate protection only.
		return true;
	}

	const uint32 ActivationSequenceId = AnimatingAbility->GetActivationSequenceId();
	if (PredictedCollisionDedupAbility.Get() != AnimatingAbility ||
		PredictedCollisionDedupActivationSequenceId != ActivationSequenceId)
	{
		PredictedCollisionDedupAbility = AnimatingAbility;
		PredictedCollisionDedupActivationSequenceId = ActivationSequenceId;
		PredictedCollisionTargetsThisActivation.Reset();
	}

	FGameplayTagContainer& ReservedTags =
		PredictedCollisionTargetsThisActivation.FindOrAdd(TargetActor);
	if (ReservedTags.HasTagExact(ReactionTag))
	{

		return false;
	}

	ReservedTags.AddTag(ReactionTag);
	return true;
}

void UPP_PredictionComponent::ReleasePredictedCollisionTarget(
	AActor* TargetActor,
	const FGameplayTag ReactionTag)
{
	if (!TargetActor || !ReactionTag.IsValid())
	{
		return;
	}

	FGameplayTagContainer* ReservedTags =
		PredictedCollisionTargetsThisActivation.Find(TargetActor);
	if (!ReservedTags)
	{
		return;
	}

	ReservedTags->RemoveTag(ReactionTag);
	if (ReservedTags->IsEmpty())
	{
		PredictedCollisionTargetsThisActivation.Remove(TargetActor);
	}
}

void UPP_PredictionComponent::ServerConfirmPredictedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 AActor* TargetActor,
 FGameplayTag ReactionTag,
 FPP_ReactionTransformSettings TransformSettings,
 FPP_ReactionDefenseSettings DefenseSettings,
 FPP_ReactionDamageSettings DamageSettings,
 FPP_ReactionGameplayCueSettings GameplayCueSettings,
 FHitResult HitResult)
{
	// Never trust the validation fields serialized by the client. Rebuild them from the
	// synchronized-time estimate using this server's current clock and configured rewind window.
	ValidateClientHitTime(Context);
	ValidateTargetPresentationTime(Context, TargetActor);


	if (bRequireAuthoritativeCollisionMatch)
	{
		AActor* OwnerActor = GetOwner();
		UWorld* World = GetWorld();
		FPP_ReactionDataEntry Reaction;
		if (!OwnerActor || !OwnerActor->HasAuthority() || !World || !Context.IsValid() || !TargetActor ||
			TargetActor == OwnerActor || TargetActor->GetWorld() != World || !Cast<ACharacter>(TargetActor) ||
			!ReactionTag.IsValid() || !ReactionData || !ReactionData->FindReaction(ReactionTag, Reaction))
		{
			return;
		}

		FPP_ReactionTransformSettings AuthoritativeTransformSettings;
		FPP_ReactionDefenseSettings AuthoritativeDefenseSettings;
		FPP_ReactionDamageSettings AuthoritativeDamageSettings;
		FPP_ReactionGameplayCueSettings AuthoritativeGameplayCueSettings;
		FHitResult AuthoritativeHitResult;
		if (ConsumeAuthoritativeCollision(TargetActor, ReactionTag,
			AuthoritativeTransformSettings, AuthoritativeDefenseSettings, AuthoritativeDamageSettings,
			AuthoritativeGameplayCueSettings, AuthoritativeHitResult))
		{
			if (!ProcessServerConfirmedReaction(Context, TargetActor, ReactionTag,
				AuthoritativeTransformSettings, AuthoritativeDefenseSettings, AuthoritativeDamageSettings,
				AuthoritativeGameplayCueSettings, AuthoritativeHitResult))
			{
				ClientRejectPredictedReaction(
					Context, TargetActor, ReactionTag, TargetActor->GetActorLocation(), TargetActor->GetActorRotation());
			}
			return;
		}

		// A server-authored notify sweep may already have run without touching the target's current
		// capsule. Validate that same geometry against synchronized server history before waiting for
		// a future live overlap. No client-provided trace shape or transform participates in this test.
		if (TryFindHistoricalCollisionSweep(Context, TargetActor, ReactionTag,
			AuthoritativeTransformSettings, AuthoritativeDefenseSettings, AuthoritativeDamageSettings,
			AuthoritativeGameplayCueSettings, AuthoritativeHitResult))
		{
			if (!ProcessServerConfirmedReaction(Context, TargetActor, ReactionTag,
				AuthoritativeTransformSettings, AuthoritativeDefenseSettings, AuthoritativeDamageSettings,
				AuthoritativeGameplayCueSettings, AuthoritativeHitResult))
			{
				ClientRejectPredictedReaction(
					Context, TargetActor, ReactionTag,
					TargetActor->GetActorLocation(), TargetActor->GetActorRotation());
			}
			return;
		}

		// The reliable RPC can arrive before the server montage reaches the same notify frame.
		QueuePendingServerReactionRequest(Context, TargetActor, ReactionTag);
		return;
	}

	if (!ProcessServerConfirmedReaction(Context, TargetActor, ReactionTag, TransformSettings,
		DefenseSettings, DamageSettings, GameplayCueSettings, HitResult))
	{
		ClientRejectPredictedReaction(
			Context, TargetActor, ReactionTag, TargetActor->GetActorLocation(), TargetActor->GetActorRotation());
	}
}

bool UPP_PredictionComponent::ProcessServerConfirmedReaction(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	const FPP_ReactionTransformSettings& TransformSettings,
	const FPP_ReactionDefenseSettings& DefenseSettings,
	const FPP_ReactionDamageSettings& DamageSettings,
	const FPP_ReactionGameplayCueSettings& GameplayCueSettings,
	const FHitResult& HitResult)
{
	// Authority repeats the transform, applies effects, then publishes start and finish.
	AActor* OwnerActor = GetOwner();


	if (!OwnerActor || !OwnerActor->HasAuthority()) return false;

	if (!Context.IsValid() || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return false;
	if (!ConsumeServerReactionRequestBudget()) return false;
	const FGameplayTag GameplayCueRootTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("GameplayCue")), false);
	if (!PP_IsValidReactionEnumValue(DefenseSettings.SuperArmor.RequiredSuperArmor) ||
		!FMath::IsFinite(DefenseSettings.Block.BlockAngleDegrees) ||
		DefenseSettings.Block.BlockAngleDegrees < 0.0f || DefenseSettings.Block.BlockAngleDegrees > 180.0f ||
		DamageSettings.DamageEffects.Num() > 16 || GameplayCueSettings.Cues.Num() > 16 ||
		HitResult.Location.ContainsNaN() || HitResult.ImpactPoint.ContainsNaN() ||
		HitResult.ImpactNormal.ContainsNaN())
	{
		return false;
	}
	for (const FPP_ReactionDamageEffect& DamageEffect : DamageSettings.DamageEffects)
	{
		if (!FMath::IsFinite(DamageEffect.EffectLevel) || DamageEffect.EffectLevel < 0.0f ||
			!FMath::IsFinite(DamageEffect.PhysicalAttackMultiplier) ||
			DamageEffect.PhysicalAttackMultiplier < 0.0f ||
			!FMath::IsFinite(DamageEffect.MagicalAttackMultiplier) ||
			DamageEffect.MagicalAttackMultiplier < 0.0f)
		{
			return false;
		}
	}
	for (const FPP_ReactionGameplayCue& Cue : GameplayCueSettings.Cues)
	{
		if (!Cue.CueTag.IsValid() || !GameplayCueRootTag.IsValid() ||
			!Cue.CueTag.MatchesTag(GameplayCueRootTag) || Cue.LocationOffset.ContainsNaN() ||
			!PP_IsValidReactionEnumValue(Cue.SpawnPoint) ||
			!PP_IsValidReactionEnumValue(Cue.TriggerTiming) ||
			!PP_IsValidReactionEnumValue(Cue.Recipient))
		{
			return false;
		}
	}

	// Resolve defense from authoritative target tags and transforms.
	const EPP_ReactionDefenseOutcome DefenseOutcome = ResolveDefenseOutcome(TargetActor, DefenseSettings);
	const FGameplayTag RequestedPlaybackReactionTag =
		DefenseOutcome == EPP_ReactionDefenseOutcome::Blocked
			? TAG_Trigger_Hit_Block_Success
			: ReactionTag;

	FGameplayTag ResolvedReactionTag;
	FPP_ReactionDataEntry Reaction;
	bool bPreserveOriginalTransform = true;
	if (!ResolveReactionForTarget(
		TargetActor, RequestedPlaybackReactionTag, ResolvedReactionTag, Reaction,
		bPreserveOriginalTransform))
	{
		return false;
	}
	if (!CanServerAcceptPredictedReaction(TargetActor, ReactionTag, TransformSettings)) return false;

	FPP_ReactionTransformSettings ResolvedTransformSettings = TransformSettings;
	if (ResolvedReactionTag != RequestedPlaybackReactionTag && !bPreserveOriginalTransform)
	{
		SuppressReactionTransform(ResolvedTransformSettings);
	}
	if (DefenseOutcome == EPP_ReactionDefenseOutcome::Blocked ||
		DefenseOutcome == EPP_ReactionDefenseOutcome::Parried)
	{
		if (!DefenseSettings.Block.bAllowMovementWhenBlocked)
		{
			ResolvedTransformSettings.MovementSettings.MoveDirection = EPP_ReactionMoveDirection::None;
		}
		if (!DefenseSettings.Block.bAllowRotationWhenBlocked)
		{
			ResolvedTransformSettings.RotationSettings.RotationDirection = EPP_ReactionRotationDirection::None;
		}
	}

	// Rebuild facing from the server controller to match the local prediction rule.
	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		ResolvedTransformSettings.RotationSettings.bUseReferenceFacingOverride = true;
		ResolvedTransformSettings.RotationSettings.ReferenceFacingOverride =
			FRotator(0.0f, OwnerPawn->GetControlRotation().Yaw, 0.0f);
	}

	if (!ValidateServerReactionRequest(TargetActor, Reaction, ResolvedTransformSettings)) return false;

	const bool bAppliedLagCompensation =
		ApplyLagCompensatedRootMotionRollback(Context, TargetActor);
	if (bAppliedLagCompensation)
	{
		ResolvedTransformSettings.MovementSettings.ClientInterpolationSpeed = FMath::Max(
			ResolvedTransformSettings.MovementSettings.ClientInterpolationSpeed,
			FMath::Max(0.0f, LagCompensationClientCorrectionSpeed));
	}

	const float StartPosition = GetReactionStartPosition(Reaction);


	const bool bCleanHit = DefenseOutcome == EPP_ReactionDefenseOutcome::None;
	if (bCleanHit || ShouldApplyReactionTransform(DefenseOutcome, DefenseSettings))
	{
		ApplyReactionTransform(OwnerActor, TargetActor, ResolvedTransformSettings);
	}

	const FVector ServerStartLocation = TargetActor->GetActorLocation();
	const FRotator ServerStartRotation = TargetActor->GetActorRotation();


	if (ShouldApplyDamage(TargetActor, DefenseOutcome, DamageSettings))
	{
		ApplyDamageEffects(TargetActor, DamageSettings, HitResult);
	}


	if (bCleanHit)
	{
		ApplyTargetEffects(OwnerActor, TargetActor, Reaction);
		const bool bServerMontagePlayed =
			PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
		if (bServerMontagePlayed && !PP_IsAdditiveReaction(Reaction))
		{
			ScheduleServerRootMotionStartAlignment(
				Context, TargetActor, ResolvedReactionTag, Reaction.Montage, StartPosition,
				ServerStartLocation);
		}
	}
	else
	{
		ApplyDefenseOutcomeEffects(TargetActor, DefenseOutcome);
		ShowDefenseOutcomeCombatTextToAttacker(TargetActor, DefenseOutcome, HitResult);

		// A successful block may still use a visual-only additive reaction. It does not
		// receive clean-hit effects, gameplay cues, cancellation, or transform correction.
		const bool bPlayBlockedAdditiveReaction =
			DefenseOutcome == EPP_ReactionDefenseOutcome::Blocked &&
			PP_IsAdditiveReaction(Reaction);
		if (bPlayBlockedAdditiveReaction)
		{
			PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

			if (UPP_PredictionComponent* TargetPredictionComponent =
				TargetActor->FindComponentByClass<UPP_PredictionComponent>())
			{
				TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(
					Context, ResolvedReactionTag, ServerStartLocation, ServerStartRotation, 0.0f);
			}

			ClientConfirmPredictedReactionStart(
				Context, TargetActor, ResolvedReactionTag, ServerStartLocation, ServerStartRotation);
		}

		MulticastPlayConfirmedReaction(Context, TargetActor, ResolvedReactionTag,
			ServerStartLocation, ServerStartRotation, 0.0f, bPlayBlockedAdditiveReaction,
			bPlayBlockedAdditiveReaction
				? FPP_ReactionGameplayCueSettings()
				: GameplayCueSettings,
			HitResult);
		return true;
	}


	const bool bNeedsMovementReconciliation = !PP_IsAdditiveReaction(Reaction);
	if (bNeedsMovementReconciliation)
	{
		if (UWorld* World = GetWorld())
		{
			const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
			const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
			const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);
			TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
			TWeakObjectPtr<AActor> WeakTarget(TargetActor);
			const FPP_ReactionPredictionContext CapturedContext = Context;
			const FGameplayTag CapturedReactionTag = ResolvedReactionTag;


			FTimerHandle TimerHandle;
			World->GetTimerManager().SetTimer(TimerHandle, [WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
			                                  {
				                                  UPP_PredictionComponent* StrongThis = WeakThis.Get();
				                                  AActor* StrongTarget = WeakTarget.Get();
				                                  if (!StrongThis || !StrongTarget) return;

				                                  const FVector ServerFinalLocation = StrongTarget->GetActorLocation();
				                                  const FRotator ServerFinalRotation = StrongTarget->GetActorRotation();

				                                  StrongThis->ClientFinishPredictedProxyReaction(
				                                  CapturedContext, StrongTarget, CapturedReactionTag,
				                                  ServerFinalLocation, ServerFinalRotation);

				                                  if (UPP_PredictionComponent* TargetPredictionComponent =
					                                  StrongTarget->FindComponentByClass<UPP_PredictionComponent>())
				                                  {
					                                  TargetPredictionComponent->ClientFinishOwnerConfirmedReaction(
						                                  CapturedContext, CapturedReactionTag,
						                                  ServerFinalLocation, ServerFinalRotation);
				                                  }
			                                  },
			                                  RemainingDuration,
			                                  false);
		}
	}

	if (UPP_PredictionComponent* TargetPredictionComponent =
		TargetActor->FindComponentByClass<UPP_PredictionComponent>())
	{
		TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(
			Context, ResolvedReactionTag, ServerStartLocation, ServerStartRotation,
			ResolvedTransformSettings.MovementSettings.ClientInterpolationSpeed);
	}

	ClientConfirmPredictedReactionStart(
		Context, TargetActor, ResolvedReactionTag, ServerStartLocation, ServerStartRotation);
	MulticastPlayConfirmedReaction(Context, TargetActor, ResolvedReactionTag,
	                               ServerStartLocation, ServerStartRotation,
	                               ResolvedTransformSettings.MovementSettings.ClientInterpolationSpeed, true,
	                               GameplayCueSettings, HitResult);
	return true;
}

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 AActor* TargetActor,
 FGameplayTag ReactionTag,
 FVector ServerStartLocation,
 FRotator ServerStartRotation,
 float ClientInterpolationSpeed,
 bool bPlayReaction,
 FPP_ReactionGameplayCueSettings GameplayCueSettings,
 FHitResult HitResult)
{
	// The predicting client consumes its marker; other proxies start from server state.
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return;
	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;
	const bool bAdditiveReaction = PP_IsAdditiveReaction(Reaction);

	// The server already played authoritative reaction state, but a listen server still needs cosmetics.
	if (OwnerActor->HasAuthority())
	{
		if (bPlayReaction)
		{
			ExecuteGameplayCuesLocally(
				TargetActor, GameplayCueSettings, HitResult,
				EPP_ReactionGameplayCueTriggerTiming::OnHit,
				EPP_GameplayCueExecutionFilter::All);
		}
		return;
	}

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();

	FGameplayTag PredictedReactionTag;
	bool bPlayedLocally = false;
	bool bPlayedGameplayCuesLocally = false;
	int32 PredictedProxyAnimTagHandle = INDEX_NONE;
	const bool bConsumedPendingPrediction =
		ConsumePendingPredictedReaction(Context, TargetActor, &PredictedReactionTag, &bPlayedLocally,
			&bPlayedGameplayCuesLocally, &PredictedProxyAnimTagHandle);
	const bool bNewerReactionOwnsTarget =
		HasNewerReactionMarkerForTarget(Context, TargetActor);

	if (!bPlayReaction)
	{
		EndPredictedProxyRootMotionReconciliation(Context, TargetActor);
		EndPredictedProxyTargetEffectFeedback(TargetActor, PredictedProxyAnimTagHandle);
		if (bNewerReactionOwnsTarget)
		{

			return;
		}

		if (bConsumedPendingPrediction && bPlayedLocally)
		{
			// Local proxy state was stale: cancel the clean montage and restore server state.
			FPP_ReactionDataEntry PredictedReaction;
			bool bRestoreServerTransform = true;
			if (ReactionData->FindReaction(PredictedReactionTag, PredictedReaction) && PredictedReaction.Montage)
			{
				bRestoreServerTransform = !PP_IsAdditiveReaction(PredictedReaction);
				if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
				{
					if (UAnimInstance* AnimInstance = TargetCharacter->GetMesh()->GetAnimInstance())
					{
						AnimInstance->Montage_Stop(0.1f, PredictedReaction.Montage);
					}
				}
			}
			if (bRestoreServerTransform)
			{
				TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
				FPP_ReactionMovementSettings CorrectionSettings;
				CorrectionSettings.ClientInterpolationSpeed = FMath::Max(0.0f, ClientInterpolationSpeed);
				SetReactionActorLocation(TargetActor, ServerStartLocation, CorrectionSettings, true);
			}
		}
		return;
	}

	const bool bMatchingLocalCuePrediction = bConsumedPendingPrediction &&
		bPlayedGameplayCuesLocally && PredictedReactionTag == ReactionTag;
	ExecuteGameplayCuesLocally(
		TargetActor, GameplayCueSettings, HitResult,
		EPP_ReactionGameplayCueTriggerTiming::OnHit,
		bMatchingLocalCuePrediction
			? EPP_GameplayCueExecutionFilter::ConfirmationOnly
			: EPP_GameplayCueExecutionFilter::All);

	if (bConsumedPendingPrediction)
	{
		if (!bAdditiveReaction)
		{
			AddDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
		}
		if (PredictedReactionTag != ReactionTag)
		{
			EndPredictedProxyTargetEffectFeedback(TargetActor, PredictedProxyAnimTagHandle);
		}
		if (bNewerReactionOwnsTarget)
		{

			return;
		}

		// Matching outcomes already played locally. A mismatch must replace the predicted montage.
		if (bPlayedLocally && PredictedReactionTag == ReactionTag)
		{
			if (!bAdditiveReaction && Reaction.Montage)
			{
				EnsurePredictedProxyReactionMontagePlaying(Context, TargetActor, Reaction);
			}
			return;
		}
	}


	if (bTargetLocallyControlled)
	{
		return;
	}

	const float StartPosition = GetReactionStartPosition(Reaction);

	ApplyLatestOlderProxyPendingFinalReactionCorrection(Context, TargetActor, TEXT("ConfirmedPreflight"));

	if (!bAdditiveReaction)
	{
		TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
		FPP_ReactionMovementSettings ConfirmedMovementSettings;
		ConfirmedMovementSettings.ClientInterpolationSpeed = FMath::Max(0.0f, ClientInterpolationSpeed);
		SetReactionActorLocation(TargetActor, ServerStartLocation, ConfirmedMovementSettings, true);
	}

	PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
}

void UPP_PredictionComponent::ClientRejectPredictedReaction_Implementation(
	FPP_ReactionPredictionContext Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	FVector ServerLocation,
	FRotator ServerRotation)
{

	TOptional<FSimulatedRootMotionReplicatedMove> QuarantinedAuthoritativeMove;
	float ObservedAuthoritativePlayRate = 0.0f;
	const bool bHadProxyRootMotionGuard = TakeQuarantinedAuthoritativeProxyRootMotionMove(
		Context, TargetActor, QuarantinedAuthoritativeMove, ObservedAuthoritativePlayRate);
	EndPredictedProxyRootMotionReconciliation(Context, TargetActor);
	const float MeshBlendDuration = FMath::Max(0.03f, RejectedPredictionProxyMeshBlendDuration);
	const float ProxyCorrectionDistance = TargetActor
		? FVector::Dist(TargetActor->GetActorLocation(), ServerLocation)
		: 0.0f;
	const float ProxyCorrectionSpeed = ProxyCorrectionDistance > KINDA_SMALL_NUMBER
		? ProxyCorrectionDistance / MeshBlendDuration
		: 0.0f;
	// Reuse the established rejection path, but deliver it reliably only to the predicting owner.
	MulticastPlayConfirmedReaction_Implementation(
		Context, TargetActor, ReactionTag, ServerLocation, ServerRotation, ProxyCorrectionSpeed, false,
		FPP_ReactionGameplayCueSettings(), FHitResult());

	// Resume the still-authoritative montage immediately. The proxy mesh correction started above
	// continues in parallel, so visual position recovery never delays the remaining animation.
	const bool bNewerReactionOwnsTarget = HasNewerReactionMarkerForTarget(Context, TargetActor);
	if (bHadProxyRootMotionGuard && !bNewerReactionOwnsTarget)
	{
		RestoreAuthoritativeProxyStateAfterRejectedPrediction(
			Context, TargetActor, ReactionTag, MoveTemp(QuarantinedAuthoritativeMove),
			ObservedAuthoritativePlayRate);
	}
}

void UPP_PredictionComponent::ClientConfirmPredictedReactionStart_Implementation(
	FPP_ReactionPredictionContext Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	FVector ServerStartLocation,
	FRotator ServerStartRotation)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || OwnerActor->HasAuthority() || !Context.IsValid() || !TargetActor ||
		!ReactionTag.IsValid())
	{
		return;
	}

	RemoveExpiredPendingPredictedReactions();

	for (const FPP_PendingPredictedReaction& Pending : PendingPredictedReactions)
	{
		if (Pending.TargetActor.Get() != TargetActor || Pending.PredictionId != Context.PredictionId ||
			Pending.ReactionTag != ReactionTag || !Pending.bPlayedLocally)
		{
			continue;
		}

		BeginPredictedProxyRootMotionReconciliation(
			Context, TargetActor, ReactionTag, true);
		return;
	}
}


bool UPP_PredictionComponent::ConsumePendingPredictedReaction
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag* OutPredictedReactionTag, bool* bOutPlayedLocally,
 bool* bOutPlayedGameplayCuesLocally,
 int32* OutPredictedProxyAnimTagHandle)
{
	if (!Context.IsValid() || !TargetActor) return false;

	RemoveExpiredPendingPredictedReactions();

	for (int32 Index = PendingPredictedReactions.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PendingPredictedReaction& Entry = PendingPredictedReactions[Index];

		if (Entry.TargetActor.Get() == TargetActor && Entry.PredictionId == Context.PredictionId)
		{
			if (OutPredictedReactionTag) *OutPredictedReactionTag = Entry.ReactionTag;
			if (bOutPlayedLocally) *bOutPlayedLocally = Entry.bPlayedLocally;
			if (bOutPlayedGameplayCuesLocally)
			{
				*bOutPlayedGameplayCuesLocally = Entry.bPlayedGameplayCuesLocally;
			}
			if (OutPredictedProxyAnimTagHandle)
			{
				*OutPredictedProxyAnimTagHandle = Entry.PredictedProxyAnimTagHandle;
			}
			PendingPredictedReactions.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::ClientPlayOwnerConfirmedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 FGameplayTag ReactionTag,
 FVector ServerStartLocation,
 FRotator ServerStartRotation,
 float ClientInterpolationSpeed)
{
	// Owner confirmation replays visuals without letting montage root motion move the capsule.
	AActor* OwnerActor = GetOwner();
	AActor* TargetActor = OwnerActor;


	if (!OwnerActor)
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid())
	{
		return;
	}

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (!TargetCharacter) return;
	if (PP_IsAdditiveReaction(Reaction))
	{
		// Additive overlays are visual-only. Keep normal owner movement and correction active.
		PlayReactionMontageOnActor(TargetActor, Reaction, GetReactionStartPosition(Reaction), true);
		return;
	}

	if (!Reaction.Montage)
	{
		// GAS drives montage-less owner animation. Only align its authoritative start transform;
		// montage/root-motion correction suppression is unnecessary for this reaction type.
		TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
		FPP_ReactionMovementSettings OwnerMovementSettings;
		OwnerMovementSettings.ClientInterpolationSpeed = FMath::Max(0.0f, ClientInterpolationSpeed);
		SetReactionActorLocation(TargetActor, ServerStartLocation, OwnerMovementSettings, true);
		return;
	}

	UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();
	UPP_CharacterMovementComponent* SyncMovementComponent =
		Cast<UPP_CharacterMovementComponent>(MovementComponent);
	USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
	UAnimInstance* OwnerAnimInstance = TargetMesh ? TargetMesh->GetAnimInstance() : nullptr;

	ERootMotionMode::Type SavedOwnerRootMotionMode = OwnerAnimInstance
		                                                 ? OwnerAnimInstance->RootMotionMode.GetValue()
		                                                 : ERootMotionMode::RootMotionFromMontagesOnly;

	const bool bSavedAbilityRootMotionSuppressed = SyncMovementComponent
		                                               ? SyncMovementComponent->IsAbilityRootMotionSuppressed()
		                                               : false;

	bool bAppliedCorrectionSuppression = false;
	bool bAppliedVisualOnlyRootMotion = false;
	bool bAppliedAbilityRootMotionSuppression = false;

	if (MovementComponent)
	{
		OwnerReactionCorrectionSuppressionCount++;
		bAppliedCorrectionSuppression = true;

		// Keep normal capsule/server correction and smoothing alive. Only prevent root-motion
		// correction RPCs from fast-forwarding the locally replayed montage track.
		PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, true);
	}

	const float StartPosition = GetReactionStartPosition(Reaction);

	TargetActor->SetActorRotation(ServerStartRotation, ETeleportType::TeleportPhysics);
	FPP_ReactionMovementSettings OwnerMovementSettings;
	OwnerMovementSettings.ClientInterpolationSpeed = FMath::Max(0.0f, ClientInterpolationSpeed);
	SetReactionActorLocation(TargetActor, ServerStartLocation, OwnerMovementSettings, true);


	if (SyncMovementComponent)
	{
		SyncMovementComponent->SetAbilityRootMotionSuppressed(true);
		bAppliedAbilityRootMotionSuppression = true;
	}

	if (OwnerAnimInstance)
	{
		SavedOwnerRootMotionMode = OwnerAnimInstance->RootMotionMode.GetValue();
		OwnerAnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		bAppliedVisualOnlyRootMotion = true;
	}

	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);


	if (!bPlayed)
	{
		if (bAppliedCorrectionSuppression)
		{
			OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);

			if (OwnerReactionCorrectionSuppressionCount == 0)
			{
				ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag);

				if (SyncMovementComponent && bAppliedAbilityRootMotionSuppression)
				{
					SyncMovementComponent->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);
				}

				if (OwnerAnimInstance && bAppliedVisualOnlyRootMotion)
				{
					OwnerAnimInstance->SetRootMotionMode(SavedOwnerRootMotionMode);
				}

				if (MovementComponent)
				{
					PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, false);
				}
			}
		}

		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);

		TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
		TWeakObjectPtr<UCharacterMovementComponent> WeakMovementComponent(MovementComponent);
		TWeakObjectPtr<UPP_CharacterMovementComponent> WeakSyncMovementComponent(SyncMovementComponent);
		TWeakObjectPtr<UAnimInstance> WeakOwnerAnimInstance(OwnerAnimInstance);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FGameplayTag CapturedReactionTag = ReactionTag;
		const FPP_ReactionPredictionContext CapturedContext = Context;
		const bool bCapturedAppliedCorrectionSuppression = bAppliedCorrectionSuppression;
		const bool bCapturedAppliedVisualOnlyRootMotion = bAppliedVisualOnlyRootMotion;
		const bool bCapturedAppliedAbilityRootMotionSuppression = bAppliedAbilityRootMotionSuppression;
		const bool bCapturedSavedAbilityRootMotionSuppressed = bSavedAbilityRootMotionSuppressed;
		const ERootMotionMode::Type CapturedSavedOwnerRootMotionMode = SavedOwnerRootMotionMode;

		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(
			TimerHandle,
			[WeakThis, WeakMovementComponent, WeakSyncMovementComponent, WeakOwnerAnimInstance, WeakTarget,
				CapturedReactionTag, CapturedContext, bCapturedAppliedCorrectionSuppression,
				bCapturedAppliedVisualOnlyRootMotion, bCapturedAppliedAbilityRootMotionSuppression,
				bCapturedSavedAbilityRootMotionSuppressed, CapturedSavedOwnerRootMotionMode]()
			{
				UPP_PredictionComponent* StrongThis = WeakThis.Get();
				AActor* StrongTargetActor = WeakTarget.Get();
				UCharacterMovementComponent* StrongMovementComponent = WeakMovementComponent.Get();
				UPP_CharacterMovementComponent* StrongSyncMovementComponent = WeakSyncMovementComponent.
					Get();
				UAnimInstance* StrongOwnerAnimInstance = WeakOwnerAnimInstance.Get();

				if (!StrongThis) return;

				if (bCapturedAppliedCorrectionSuppression)
				{
					StrongThis->OwnerReactionCorrectionSuppressionCount =
						FMath::Max(0, StrongThis->OwnerReactionCorrectionSuppressionCount - 1);
				}


				if (StrongThis->OwnerReactionCorrectionSuppressionCount > 0)
				{
					return;
				}

				if (StrongSyncMovementComponent && bCapturedAppliedAbilityRootMotionSuppression)
				{
					StrongSyncMovementComponent->SetAbilityRootMotionSuppressed(
						bCapturedSavedAbilityRootMotionSuppressed);
				}

				if (StrongOwnerAnimInstance && bCapturedAppliedVisualOnlyRootMotion)
				{
					StrongOwnerAnimInstance->SetRootMotionMode(CapturedSavedOwnerRootMotionMode);
				}

				// A large early final is blended while ordinary movement correction is still suppressed.
				// Small or late finals preserve the existing replication-settle path.
				bool bHoldMovementCorrectionsForFinalBlend = false;
				if (StrongTargetActor)
				{
					bHoldMovementCorrectionsForFinalBlend = StrongThis->BeginOwnerFinalReconciliationGrace(
						CapturedContext, StrongTargetActor, CapturedReactionTag);
				}

				if (StrongMovementComponent && bCapturedAppliedCorrectionSuppression &&
					!bHoldMovementCorrectionsForFinalBlend)
				{
					PP_SetOwnerMontageTrackCorrectionSuppressed(StrongMovementComponent, false);
				}
			},
			RemainingDuration,
			false);
	}
	else
	{
		if (bAppliedCorrectionSuppression)
		{
			OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);
		}

		if (OwnerReactionCorrectionSuppressionCount == 0)
		{
			ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag);

			if (SyncMovementComponent && bAppliedAbilityRootMotionSuppression)
			{
				SyncMovementComponent->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);
			}

			if (OwnerAnimInstance && bAppliedVisualOnlyRootMotion)
			{
				OwnerAnimInstance->SetRootMotionMode(SavedOwnerRootMotionMode);
			}

			if (MovementComponent)
			{
				PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, false);
			}
		}
	}
}


void UPP_PredictionComponent::ClientFinishOwnerConfirmedReaction_Implementation
(FPP_ReactionPredictionContext Context,
 FGameplayTag ReactionTag,
 FVector ServerFinalLocation,
 FRotator ServerFinalRotation)
{
	AActor* OwnerActor = GetOwner();
	AActor* TargetActor = OwnerActor;
	if (!OwnerActor || OwnerActor->HasAuthority() ||
		!Context.IsValid() || !ReactionTag.IsValid())
	{
		return;
	}

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (!TargetPawn || !TargetPawn->IsLocallyControlled()) return;
	FPP_ReactionDataEntry Reaction;
	if (ReactionData && ReactionData->FindReaction(ReactionTag, Reaction) &&
		PP_IsAdditiveReaction(Reaction))
	{
		return;
	}


	AddOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
		ServerFinalLocation, ServerFinalRotation);

	if (OwnerReactionCorrectionSuppressionCount == 0)
	{
		if (IsOwnerFinalReconciliationGraceActive(Context, TargetActor, ReactionTag))
		{
			return;
		}

		ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag);
	}
}

void UPP_PredictionComponent::ClientFinishPredictedProxyReaction_Implementation
(FPP_ReactionPredictionContext Context,
 AActor* TargetActor,
 FGameplayTag ReactionTag,
 FVector ServerFinalLocation,
 FRotator ServerFinalRotation)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || OwnerActor->HasAuthority() || !TargetActor ||
		!Context.IsValid() || !ReactionTag.IsValid())
	{
		return;
	}
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (TargetPawn && TargetPawn->IsLocallyControlled()) return;
	FPP_ReactionDataEntry Reaction;
	if (ReactionData && ReactionData->FindReaction(ReactionTag, Reaction) &&
		PP_IsAdditiveReaction(Reaction))
	{
		ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
		ConsumePendingPredictedReaction(Context, TargetActor);
		return;
	}

	const bool bHadDeferredCorrection =
		ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
	const bool bHadPendingPrediction =
		ConsumePendingPredictedReaction(Context, TargetActor);



	if (!bHadDeferredCorrection && !bHadPendingPrediction)
	{
		return;
	}

	AddProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, ServerFinalLocation, ServerFinalRotation);

	UWorld* World = GetWorld();
	if (!World || ProxyFinalCorrectionDelaySeconds <= KINDA_SMALL_NUMBER)
	{
		ApplyProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("ProxyFinalNoDelay"));
		return;
	}

	TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
	TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	const FPP_ReactionPredictionContext CapturedContext = Context;
	const FGameplayTag CapturedReactionTag = ReactionTag;

	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(
		TimerHandle,
		[WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
		{
			UPP_PredictionComponent* StrongThis = WeakThis.Get();
			AActor* StrongTarget = WeakTarget.Get();
			if (!StrongThis || !StrongTarget) return;

			StrongThis->ApplyProxyPendingFinalReactionCorrection(CapturedContext, StrongTarget,
			                                                     CapturedReactionTag, TEXT("ProxyFinalDelayElapsed"));
		},
		ProxyFinalCorrectionDelaySeconds,
		false);
}


void UPP_PredictionComponent::AddDeferredPredictedReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredDeferredPredictedReactionCorrections();

	FPP_DeferredPredictedReactionCorrection& Entry =
		DeferredPredictedReactionCorrections.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.TimeSeconds = World->GetTimeSeconds();
}

bool UPP_PredictionComponent::ConsumeDeferredPredictedReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredDeferredPredictedReactionCorrections();

	for (int32 Index = DeferredPredictedReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_DeferredPredictedReactionCorrection& Entry = DeferredPredictedReactionCorrections[Index];

		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			DeferredPredictedReactionCorrections.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::RemoveExpiredDeferredPredictedReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		DeferredPredictedReactionCorrections.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = DeferredPredictedReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_DeferredPredictedReactionCorrection& Entry = DeferredPredictedReactionCorrections[Index];

		const bool bExpired = Now - Entry.TimeSeconds > DeferredPredictedCorrectionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			DeferredPredictedReactionCorrections.RemoveAtSwap(Index);
		}
	}
}

void UPP_PredictionComponent::AddOwnerPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const FVector& ServerFinalLocation,
 const FRotator& ServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredOwnerPendingFinalReactionCorrections();

	for (FPP_OwnerPendingFinalReactionCorrection& Entry : OwnerPendingFinalReactionCorrections)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			Entry.ServerFinalLocation = ServerFinalLocation;
			Entry.ServerFinalRotation = ServerFinalRotation;
			Entry.TimeSeconds = World->GetTimeSeconds();
			return;
		}
	}

	FPP_OwnerPendingFinalReactionCorrection& Entry =
		OwnerPendingFinalReactionCorrections.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.ServerFinalLocation = ServerFinalLocation;
	Entry.ServerFinalRotation = ServerFinalRotation;
	Entry.TimeSeconds = World->GetTimeSeconds();
}

bool UPP_PredictionComponent::ConsumeOwnerPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 FVector& OutServerFinalLocation,
 FRotator& OutServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredOwnerPendingFinalReactionCorrections();

	for (int32 Index = OwnerPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = OwnerPendingFinalReactionCorrections[Index];

		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			OutServerFinalLocation = Entry.ServerFinalLocation;
			OutServerFinalRotation = Entry.ServerFinalRotation;
			OwnerPendingFinalReactionCorrections.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

bool UPP_PredictionComponent::PeekOwnerPendingFinalReactionCorrection(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag,
	FVector& OutServerFinalLocation,
	FRotator& OutServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredOwnerPendingFinalReactionCorrections();

	for (int32 Index = OwnerPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = OwnerPendingFinalReactionCorrections[Index];
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			OutServerFinalLocation = Entry.ServerFinalLocation;
			OutServerFinalRotation = Entry.ServerFinalRotation;
			return true;
		}
	}

	return false;
}

bool UPP_PredictionComponent::ApplyOwnerPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag)
{
	if (!TargetActor) return false;

	FVector ServerFinalLocation = FVector::ZeroVector;
	FRotator ServerFinalRotation = FRotator::ZeroRotator;

	if (!ConsumeOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
	                                                ServerFinalLocation, ServerFinalRotation))
	{
		return false;
	}

	StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor>(TargetActor));

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FRotator ClientFinalRotation = TargetActor->GetActorRotation();
	const float Distance = FVector::Dist(ClientFinalLocation, ServerFinalLocation);
	const float RotationDistance = FMath::Max3(
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Pitch - ClientFinalRotation.Pitch)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Yaw - ClientFinalRotation.Yaw)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Roll - ClientFinalRotation.Roll)));
	const bool bShouldApplyLocation = Distance > FinalCorrectionTolerance;
	const bool bRotationCorrectionRequested =
		RotationDistance > FinalRotationCorrectionTolerance;
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	UGameplayAbility* RotationOwningAbility = TargetASC
		? TargetASC->GetAnimatingAbility() : nullptr;
	UAnimMontage* RotationOwningMontage =
		RotationOwningAbility && RotationOwningAbility->IsActive()
			? RotationOwningAbility->GetCurrentMontage() : nullptr;
	const bool bRotationBlockedByActiveAbility =
		RotationOwningAbility && RotationOwningAbility->IsActive() && RotationOwningMontage;
	const bool bShouldApplyRotation =
		bRotationCorrectionRequested && !bRotationBlockedByActiveAbility;
	const float SmoothSeconds = FMath::Max(0.f, OwnerFinalCorrectionSmoothSeconds);



	if (!bShouldApplyLocation && !bShouldApplyRotation)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World || SmoothSeconds <= KINDA_SMALL_NUMBER)
	{
		TargetActor->SetActorLocationAndRotation(
			bShouldApplyLocation ? ServerFinalLocation : ClientFinalLocation,
			bShouldApplyRotation ? ServerFinalRotation : ClientFinalRotation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);


		return true;
	}

	TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	const FVector StartLocation = ClientFinalLocation;
	const FQuat StartQuat = ClientFinalRotation.Quaternion();
	const FVector TargetLocation = ServerFinalLocation;
	const FQuat TargetQuat = ServerFinalRotation.Quaternion();
	const float StartTime = World->GetTimeSeconds();
	TWeakObjectPtr<UPP_PredictionComponent> WeakPredictionComponent(this);
	TSharedRef<bool> bRotationCancelledByNewAbility = MakeShared<bool>(false);

	TSharedRef<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();

	World->GetTimerManager().SetTimer(
		TimerHandle.Get(),
		[WeakTarget, StartLocation, StartQuat, TargetLocation, TargetQuat, StartTime, SmoothSeconds,
			bShouldApplyLocation, bShouldApplyRotation,
			WeakPredictionComponent, bRotationCancelledByNewAbility, TimerHandle]()
		{
			AActor* StrongTarget = WeakTarget.Get();
			if (!StrongTarget)
			{
				return;
			}

			UWorld* InnerWorld = StrongTarget->GetWorld();
			if (!InnerWorld)
			{
				return;
			}

			const float RawAlpha = FMath::Clamp((InnerWorld->GetTimeSeconds() - StartTime) / SmoothSeconds, 0.f, 1.f);
			const float SmoothAlpha = RawAlpha * RawAlpha * (3.f - 2.f * RawAlpha);

			if (bShouldApplyRotation && !*bRotationCancelledByNewAbility)
			{
				if (UPP_PredictionComponent* PredictionComponent = WeakPredictionComponent.Get())
				{
					UAbilitySystemComponent* InnerASC =
						PredictionComponent->GetAbilitySystemComponentFromActor(StrongTarget);
					UGameplayAbility* InnerAnimatingAbility = InnerASC
						? InnerASC->GetAnimatingAbility() : nullptr;
					UAnimMontage* InnerAbilityMontage =
						InnerAnimatingAbility && InnerAnimatingAbility->IsActive()
							? InnerAnimatingAbility->GetCurrentMontage() : nullptr;
					if (InnerAnimatingAbility && InnerAnimatingAbility->IsActive() && InnerAbilityMontage)
					{
						*bRotationCancelledByNewAbility = true;

					}
				}
			}

			const bool bApplyRotationThisTick =
				bShouldApplyRotation && !*bRotationCancelledByNewAbility;

			const FVector CurrentLocation = bShouldApplyLocation
				                                ? FMath::Lerp(StartLocation, TargetLocation, SmoothAlpha)
				                                : StrongTarget->GetActorLocation();

			const FRotator CurrentRotation = bApplyRotationThisTick
				                                 ? FQuat::Slerp(StartQuat, TargetQuat, SmoothAlpha).Rotator()
				                                 : StrongTarget->GetActorRotation();

			StrongTarget->SetActorLocationAndRotation(CurrentLocation, CurrentRotation, false, nullptr,
			                                          ETeleportType::TeleportPhysics);

			if (RawAlpha >= 1.f)
			{
				StrongTarget->SetActorLocationAndRotation(
					bShouldApplyLocation ? TargetLocation : StrongTarget->GetActorLocation(),
					bApplyRotationThisTick ? TargetQuat.Rotator() : StrongTarget->GetActorRotation(),
					false,
					nullptr,
					ETeleportType::TeleportPhysics);

				InnerWorld->GetTimerManager().ClearTimer(TimerHandle.Get());
			}
		},
		0.016f,
		true);

	return true;
}

void UPP_PredictionComponent::RemoveExpiredOwnerPendingFinalReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		OwnerPendingFinalReactionCorrections.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = OwnerPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = OwnerPendingFinalReactionCorrections[Index];

		const bool bExpired = Now - Entry.TimeSeconds > DeferredPredictedCorrectionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			OwnerPendingFinalReactionCorrections.RemoveAtSwap(Index);
		}
	}
}

bool UPP_PredictionComponent::IsOwnerFinalReconciliationGraceActive(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag) const
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	for (const FPP_OwnerFinalReconciliationGraceState& State : OwnerFinalReconciliationGraceStates)
	{
		if (State.TargetActor.Get() == TargetActor &&
			State.ReactionTag == ReactionTag &&
			State.PredictionId == Context.PredictionId)
		{
			return true;
		}
	}

	return false;
}

bool UPP_PredictionComponent::BeginOwnerFinalReconciliationGrace(
	const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor,
	FGameplayTag ReactionTag)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	for (const FPP_OwnerFinalReconciliationGraceState& Existing : OwnerFinalReconciliationGraceStates)
	{
		if (Existing.TargetActor.Get() == TargetActor &&
			Existing.ReactionTag == ReactionTag &&
			Existing.PredictionId == Context.PredictionId)
		{
			return Existing.bHoldMovementCorrections;
		}
	}

	FVector ServerFinalLocation = FVector::ZeroVector;
	FRotator ServerFinalRotation = FRotator::ZeroRotator;
	const bool bHasEarlyFinal = PeekOwnerPendingFinalReactionCorrection(
		Context, TargetActor, ReactionTag, ServerFinalLocation, ServerFinalRotation);
	const float LocationGap = bHasEarlyFinal
		? FVector::Dist(TargetActor->GetActorLocation(), ServerFinalLocation)
		: 0.0f;
	const float RotationGap = bHasEarlyFinal
		? PP_GetRotationDistance(TargetActor->GetActorRotation(), ServerFinalRotation)
		: 0.0f;
	const float PreReleaseDistanceThreshold = FMath::Max(
		FinalCorrectionTolerance, OwnerFinalPreReleaseSmoothMinDistance);
	UPP_CharacterMovementComponent* MovementComponent = Cast<UPP_CharacterMovementComponent>(
		Cast<ACharacter>(TargetActor) ? Cast<ACharacter>(TargetActor)->GetCharacterMovement() : nullptr);
	const float SmoothSeconds = FMath::Max(0.0f, OwnerFinalCorrectionSmoothSeconds);
	const bool bLargeEarlyGap =
		bHasEarlyFinal &&
		(LocationGap > PreReleaseDistanceThreshold ||
		 RotationGap > FinalRotationCorrectionTolerance);
	const bool bHoldMovementCorrections =
		bLargeEarlyGap && SmoothSeconds > KINDA_SMALL_NUMBER && MovementComponent &&
		MovementComponent->ShouldIgnoreServerRootMotionMontageTrackCorrection();

	UWorld* World = GetWorld();
	const float GraceSeconds = FMath::Max(0.0f, OwnerFinalReconciliationGraceSeconds);
	if (!World || GraceSeconds <= KINDA_SMALL_NUMBER)
	{
		ApplyOwnerPendingFinalReactionCorrection(
			Context, TargetActor, ReactionTag);
		return false;
	}

	FPP_OwnerFinalReconciliationGraceState& State =
		OwnerFinalReconciliationGraceStates.AddDefaulted_GetRef();
	State.TargetActor = TargetActor;
	State.MovementComponent = MovementComponent;
	State.ReactionTag = ReactionTag;
	State.PredictionId = Context.PredictionId;
	State.bHoldMovementCorrections = bHoldMovementCorrections;

	if (bHoldMovementCorrections &&
		!ApplyOwnerPendingFinalReactionCorrection(
			Context, TargetActor, ReactionTag))
	{
		OwnerFinalReconciliationGraceStates.Pop(EAllowShrinking::No);
		return false;
	}

	TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
	TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	TWeakObjectPtr<UCharacterMovementComponent> WeakMovementComponent(MovementComponent);
	const FPP_ReactionPredictionContext CapturedContext = Context;
	const FGameplayTag CapturedReactionTag = ReactionTag;
	const float ReconciliationSeconds = bHoldMovementCorrections
		? FMath::Max(GraceSeconds, SmoothSeconds + (1.0f / 60.0f))
		: GraceSeconds;

	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(
		TimerHandle,
		[WeakThis, WeakTarget, WeakMovementComponent, CapturedContext, CapturedReactionTag]()
		{
			UPP_PredictionComponent* StrongThis = WeakThis.Get();
			AActor* StrongTarget = WeakTarget.Get();
			if (!StrongThis) return;

			bool bRemovedGrace = false;
			bool bHeldMovementCorrections = false;
			for (int32 Index = StrongThis->OwnerFinalReconciliationGraceStates.Num() - 1;
				 Index >= 0; --Index)
			{
				const FPP_OwnerFinalReconciliationGraceState& Existing =
					StrongThis->OwnerFinalReconciliationGraceStates[Index];
				if (Existing.TargetActor.Get() == StrongTarget &&
					Existing.ReactionTag == CapturedReactionTag &&
					Existing.PredictionId == CapturedContext.PredictionId)
				{
					bHeldMovementCorrections = Existing.bHoldMovementCorrections;
					StrongThis->OwnerFinalReconciliationGraceStates.RemoveAtSwap(
						Index, 1, EAllowShrinking::No);
					bRemovedGrace = true;
					break;
				}
			}

			if (!bRemovedGrace) return;

			if (bHeldMovementCorrections && StrongThis->OwnerReactionCorrectionSuppressionCount == 0)
			{
				PP_SetOwnerMontageTrackCorrectionSuppressed(WeakMovementComponent.Get(), false);
			}

			if (!StrongTarget) return;

			// A newer overlapping reaction owns reconciliation until it ends. Never apply this older
			// transform in the middle of the newer reaction.
			if (StrongThis->OwnerReactionCorrectionSuppressionCount == 0)
			{
				StrongThis->ApplyOwnerPendingFinalReactionCorrection(
					CapturedContext, StrongTarget, CapturedReactionTag);
			}
		},
		ReconciliationSeconds,
		false);

	return bHoldMovementCorrections;
}


void UPP_PredictionComponent::AddProxyPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const FVector& ServerFinalLocation,
 const FRotator& ServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredProxyPendingFinalReactionCorrections();

	for (FPP_OwnerPendingFinalReactionCorrection& Entry : ProxyPendingFinalReactionCorrections)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			Entry.ServerFinalLocation = ServerFinalLocation;
			Entry.ServerFinalRotation = ServerFinalRotation;
			Entry.TimeSeconds = World->GetTimeSeconds();


			return;
		}
	}

	FPP_OwnerPendingFinalReactionCorrection& Entry =
		ProxyPendingFinalReactionCorrections.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.ServerFinalLocation = ServerFinalLocation;
	Entry.ServerFinalRotation = ServerFinalRotation;
	Entry.TimeSeconds = World->GetTimeSeconds();
}

bool UPP_PredictionComponent::ConsumeProxyPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 FVector& OutServerFinalLocation,
 FRotator& OutServerFinalRotation)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredProxyPendingFinalReactionCorrections();

	for (int32 Index = ProxyPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = ProxyPendingFinalReactionCorrections[Index];

		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.ReactionTag == ReactionTag &&
			Entry.PredictionId == Context.PredictionId)
		{
			OutServerFinalLocation = Entry.ServerFinalLocation;
			OutServerFinalRotation = Entry.ServerFinalRotation;
			ProxyPendingFinalReactionCorrections.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

bool UPP_PredictionComponent::ApplyProxyPendingFinalReactionCorrection
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag,
 const TCHAR* Reason)
{
	if (!TargetActor) return false;

	const bool bPreflightApply = Reason && FCString::Stristr(Reason, TEXT("Preflight")) != nullptr;
	if (!bPreflightApply && HasNewerReactionMarkerForTarget(Context, TargetActor))
	{
		FVector DroppedServerFinalLocation = FVector::ZeroVector;
		FRotator DroppedServerFinalRotation = FRotator::ZeroRotator;
		ConsumeProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
		                                           DroppedServerFinalLocation, DroppedServerFinalRotation);

		return false;
	}

	FVector ServerFinalLocation = FVector::ZeroVector;
	FRotator ServerFinalRotation = FRotator::ZeroRotator;

	if (!ConsumeProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
	                                                ServerFinalLocation, ServerFinalRotation))
	{
		return false;
	}

	StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor>(TargetActor));

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FRotator ClientFinalRotation = TargetActor->GetActorRotation();
	const float Distance = FVector::Dist(ClientFinalLocation, ServerFinalLocation);
	const float RotationDistance = FMath::Max3(
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Pitch - ClientFinalRotation.Pitch)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Yaw - ClientFinalRotation.Yaw)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Roll - ClientFinalRotation.Roll)));



	if (Distance <= FinalCorrectionTolerance && RotationDistance <= FinalRotationCorrectionTolerance)
	{

		return true;
	}

	if (!bApplyInstantFinalCorrection)
	{

		return false;
	}

	if (MaxInstantFinalCorrectionDistance > 0.0f && Distance > MaxInstantFinalCorrectionDistance)
	{
		// Large errors are left to normal Character Movement replication.

		return false;
	}

	if (Distance > FinalCorrectionTolerance)
	{
		TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (RotationDistance > FinalRotationCorrectionTolerance)
	{
		TargetActor->SetActorRotation(ServerFinalRotation, ETeleportType::TeleportPhysics);
	}


	return true;
}

bool UPP_PredictionComponent::ApplyLatestOlderProxyPendingFinalReactionCorrection
(
	const FPP_ReactionPredictionContext& Context, AActor* TargetActor, const TCHAR* Reason)
{
	if (!Context.IsValid() || !TargetActor) return false;

	RemoveExpiredProxyPendingFinalReactionCorrections();

	int32 BestIndex = INDEX_NONE;
	int32 BestPredictionId = INDEX_NONE;

	for (int32 Index = 0; Index < ProxyPendingFinalReactionCorrections.Num(); ++Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = ProxyPendingFinalReactionCorrections[Index];

		if (Entry.TargetActor.Get() != TargetActor) continue;
		if (!Entry.ReactionTag.IsValid()) continue;
		if (Entry.PredictionId == INDEX_NONE) continue;
		if (Entry.PredictionId >= Context.PredictionId) continue;

		if (BestIndex == INDEX_NONE || Entry.PredictionId > BestPredictionId)
		{
			BestIndex = Index;
			BestPredictionId = Entry.PredictionId;
		}
	}

	if (BestIndex == INDEX_NONE) return false;

	const FPP_OwnerPendingFinalReactionCorrection Entry = ProxyPendingFinalReactionCorrections[BestIndex];


	FPP_ReactionPredictionContext OlderContext;
	OlderContext.PredictionId = Entry.PredictionId;

	return ApplyProxyPendingFinalReactionCorrection(OlderContext, TargetActor, Entry.ReactionTag, Reason);
}

bool UPP_PredictionComponent::HasNewerReactionMarkerForTarget
(
	const FPP_ReactionPredictionContext& Context, AActor* TargetActor)
{
	if (!Context.IsValid() || !TargetActor) return false;

	RemoveExpiredPendingPredictedReactions();
	RemoveExpiredDeferredPredictedReactionCorrections();

	for (const FPP_PendingPredictedReaction& Entry : PendingPredictedReactions)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.PredictionId != INDEX_NONE &&
			Entry.PredictionId > Context.PredictionId)
		{
			return true;
		}
	}

	for (const FPP_DeferredPredictedReactionCorrection& Entry : DeferredPredictedReactionCorrections)
	{
		if (Entry.TargetActor.Get() == TargetActor &&
			Entry.PredictionId != INDEX_NONE &&
			Entry.PredictionId > Context.PredictionId)
		{
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::RemoveExpiredProxyPendingFinalReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		ProxyPendingFinalReactionCorrections.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = ProxyPendingFinalReactionCorrections.Num() - 1; Index >= 0; --Index)
	{
		const FPP_OwnerPendingFinalReactionCorrection& Entry = ProxyPendingFinalReactionCorrections[Index];

		const bool bExpired = Now - Entry.TimeSeconds > DeferredPredictedCorrectionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			ProxyPendingFinalReactionCorrections.RemoveAtSwap(Index);
		}
	}
}

bool UPP_PredictionComponent::CanPlayPredictedReactionOnTargetProxy
(AActor* TargetActor,
 const FPP_ReactionDataEntry& Reaction) const
{
	if (!TargetActor) return false;

	const UWorld* World = GetWorld();
	if (!World) return false;

	if (World->GetNetMode() == NM_DedicatedServer) return false;

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return false;

	// Only the attacking client predicts a reaction on its remote target proxy.
	if (OwnerActor->HasAuthority()) return false;

	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return false;

	// The target we are animating should be a proxy on this machine.
	if (TargetActor->HasAuthority()) return false;

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (TargetPawn && TargetPawn->IsLocallyControlled()) return false;

	const double Now = World->GetTimeSeconds();

	if (const double* LastTime = LastReactionTimeByTarget.Find(TargetActor))
	{
		if (Now - *LastTime < Reaction.MinReplayInterval)
		{
			return false;
		}
	}

	return true;
}

FPP_ReactionPredictionContext UPP_PredictionComponent::MakeReactionPredictionContext(AActor* TargetActor)
{
	FPP_ReactionPredictionContext Context;
	Context.PredictionId = NextPredictionId;
	if (const UWorld* World = GetWorld())
	{
		const AGameStateBase* GameState = World->GetGameState();
		Context.ClientEstimatedServerTimeSeconds = GameState
			? GameState->GetServerWorldTimeSeconds()
			: World->GetTimeSeconds();
	}

	// Capture the server-issued transform marker before local reaction prediction moves the proxy.
	// The server resolves this opaque value against its own target history; no client transform is
	// accepted. Zero means Character Movement has not delivered a usable snapshot yet.
	const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (TargetCharacter && !TargetCharacter->HasAuthority())
	{
		const float MovementTimeStamp =
			TargetCharacter->GetReplicatedServerLastTransformUpdateTimeStamp();
		if (FMath::IsFinite(MovementTimeStamp) && MovementTimeStamp > 0.0f)
		{
			Context.TargetPresentationMovementTimeStamp = MovementTimeStamp;
		}
	}

	NextPredictionId = (NextPredictionId + 1) % MaxPredictionId;
	if (NextPredictionId == INDEX_NONE)
	{
		NextPredictionId = 0;
	}

	return Context;
}

void UPP_PredictionComponent::AddPendingPredictedReaction
(const FPP_ReactionPredictionContext& Context,
 AActor* TargetActor, FGameplayTag ReactionTag, const bool bPlayedLocally,
 const bool bPlayedGameplayCuesLocally,
 const int32 PredictedProxyAnimTagHandle)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	RemoveExpiredPendingPredictedReactions();

	FPP_PendingPredictedReaction& Entry = PendingPredictedReactions.AddDefaulted_GetRef();

	Entry.TargetActor = TargetActor;
	Entry.ReactionTag = ReactionTag;
	Entry.PredictionId = Context.PredictionId;
	Entry.TimeSeconds = World->GetTimeSeconds();
	Entry.bPlayedLocally = bPlayedLocally;
	Entry.bPlayedGameplayCuesLocally = bPlayedGameplayCuesLocally;
	Entry.PredictedProxyAnimTagHandle = PredictedProxyAnimTagHandle;


	if (PendingPredictedReactions.Num() > MaxPendingPredictedReactions)
	{
		const int32 NumToRemove = PendingPredictedReactions.Num() - MaxPendingPredictedReactions;
		for (int32 Index = 0; Index < NumToRemove; ++Index)
		{
			const FPP_PendingPredictedReaction& Removed = PendingPredictedReactions[Index];
			EndPredictedProxyTargetEffectFeedback(
				Removed.TargetActor.Get(), Removed.PredictedProxyAnimTagHandle);
		}
		PendingPredictedReactions.RemoveAt(0, NumToRemove, EAllowShrinking::No);
	}
}

int32 UPP_PredictionComponent::BeginPredictedProxyTargetEffectFeedback(
	AActor* TargetActor, const FPP_ReactionDataEntry& Reaction) const
{
	UPP_CombatComponent* TargetCombat =
		TargetActor ? TargetActor->FindComponentByClass<UPP_CombatComponent>() : nullptr;
	if (!TargetCombat)
	{
		return INDEX_NONE;
	}

	FGameplayTagContainer PredictedTags;
	for (const TSubclassOf<UGameplayEffect>& EffectClass : Reaction.TargetEffects)
	{
		const UGameplayEffect* Effect = EffectClass ? EffectClass.GetDefaultObject() : nullptr;
		if (Effect)
		{
			PredictedTags.AppendTags(Effect->GetGrantedTags());
		}
	}

	return TargetCombat->BeginPredictedProxyAnimTags(
		PredictedTags, PredictedProxyAnimTagTimeout);
}

void UPP_PredictionComponent::EndPredictedProxyTargetEffectFeedback(
	AActor* TargetActor, const int32 PredictionHandle)
{
	if (PredictionHandle == INDEX_NONE || !TargetActor)
	{
		return;
	}

	if (UPP_CombatComponent* TargetCombat =
		TargetActor->FindComponentByClass<UPP_CombatComponent>())
	{
		TargetCombat->EndPredictedProxyAnimTags(PredictionHandle);
	}
}

void UPP_PredictionComponent::RemoveExpiredPendingPredictedReactions()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		for (const FPP_PendingPredictedReaction& Pending : PendingPredictedReactions)
		{
			EndPredictedProxyTargetEffectFeedback(
				Pending.TargetActor.Get(), Pending.PredictedProxyAnimTagHandle);
		}
		PendingPredictedReactions.Reset();
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = PendingPredictedReactions.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PendingPredictedReaction& Entry = PendingPredictedReactions[Index];

		const bool bExpired = Now - Entry.TimeSeconds > PendingPredictedReactionTimeout;
		const bool bInvalid =
			!Entry.TargetActor.IsValid() ||
			!Entry.ReactionTag.IsValid() ||
			Entry.PredictionId == INDEX_NONE;

		if (bExpired || bInvalid)
		{
			EndPredictedProxyTargetEffectFeedback(
				Entry.TargetActor.Get(), Entry.PredictedProxyAnimTagHandle);
			PendingPredictedReactions.RemoveAtSwap(Index);
		}
	}
}

float UPP_PredictionComponent::GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const
{
	if (!Reaction.Montage || Reaction.StartSection == NAME_None)
	{
		return 0.f;
	}

	const int32 SectionIndex = Reaction.Montage->GetSectionIndex(Reaction.StartSection);
	if (SectionIndex == INDEX_NONE)
	{
		return 0.f;
	}

	float SectionStartTime = 0.f;
	float SectionEndTime = 0.f;

	Reaction.Montage->GetSectionStartAndEndTime(SectionIndex, SectionStartTime, SectionEndTime);

	return SectionStartTime;
}

void UPP_PredictionComponent::RefreshPredictedProxyReconciliationAnimationState(
	ACharacter* TargetCharacter) const
{
	if (!TargetCharacter) return;

	USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
	UPP_AnimInstance* AnimInstance = Mesh
		? Cast<UPP_AnimInstance>(Mesh->GetAnimInstance())
		: nullptr;
	if (!AnimInstance) return;

	const bool bReconciliationActive = PredictedProxyRootMotionReconciliations.ContainsByPredicate(
		[TargetCharacter](const FPP_PredictedProxyRootMotionReconciliation& State)
		{
			return State.TargetCharacter.Get() == TargetCharacter;
		});

	AnimInstance->SetPredictedProxyReconciliationActive(bReconciliationActive);
}

bool UPP_PredictionComponent::PlayReactionMontageOnActor
(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction,
 float StartPosition, bool bForceRestart) const
{
	if (!TargetActor || !Reaction.Montage) return false;

	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (!TargetCharacter) return false;

	USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
	if (!Mesh) return false;

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance) return false;

	const bool bWasPlaying = AnimInstance->Montage_IsPlaying(Reaction.Montage);
	if (!bForceRestart && bWasPlaying)
	{
		return true;
	}

	if (bForceRestart && bWasPlaying)
	{
		AnimInstance->Montage_Stop(0.f, Reaction.Montage);
	}

	const float PlayedLength = AnimInstance->Montage_Play(Reaction.Montage, Reaction.PlayRate);

	if (PlayedLength <= 0.f)
	{

		return false;
	}

	const float MontageLength = Reaction.Montage->GetPlayLength();
	const float ClampedStartPosition = FMath::Clamp(StartPosition,
	                                                0.f, FMath::Max(0.f, MontageLength - KINDA_SMALL_NUMBER));

	if (ClampedStartPosition > KINDA_SMALL_NUMBER)
	{
		AnimInstance->Montage_SetPosition(Reaction.Montage, ClampedStartPosition);
	}
	else if (Reaction.StartSection != NAME_None)
	{
		AnimInstance->Montage_JumpToSection(Reaction.StartSection, Reaction.Montage);
	}



	return true;
}

void UPP_PredictionComponent::ApplyTargetEffects
(AActor* InstigatorActor, AActor* TargetActor,
 const FPP_ReactionDataEntry& Reaction) const
{
	if (!InstigatorActor || !TargetActor || Reaction.TargetEffects.IsEmpty()) return;

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActor(InstigatorActor);
	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor);
	if (!TargetASC) return;

	UAbilitySystemComponent* SpecSourceASC = SourceASC ? SourceASC : TargetASC;
	for (const TSubclassOf<UGameplayEffect>& EffectClass : Reaction.TargetEffects)
	{
		if (!EffectClass) continue;

		FGameplayEffectContextHandle Context = SpecSourceASC->MakeEffectContext();
		Context.AddSourceObject(InstigatorActor);

		FGameplayEffectSpecHandle Spec = SpecSourceASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
		if (Spec.IsValid())
		{
			SpecSourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		}
	}
}

bool UPP_PredictionComponent::ResolveReactionForTarget(
	AActor* TargetActor,
	FGameplayTag RequestedReactionTag,
	FGameplayTag& OutResolvedReactionTag,
	FPP_ReactionDataEntry& OutReaction,
	bool& bOutPreserveOriginalTransform) const
{
	if (!ReactionData || !RequestedReactionTag.IsValid()) return false;

	FGameplayTagContainer TargetOwnedTags;
	if (UAbilitySystemComponent* TargetASC = GetAbilitySystemComponentFromActor(TargetActor))
	{
		TargetASC->GetOwnedGameplayTags(TargetOwnedTags);
	}

	return ReactionData->ResolveReaction(
		RequestedReactionTag,
		TargetOwnedTags,
		OutResolvedReactionTag,
		OutReaction,
		bOutPreserveOriginalTransform);
}

void UPP_PredictionComponent::SuppressReactionTransform(
	FPP_ReactionTransformSettings& TransformSettings)
{
	TransformSettings.MovementSettings.MoveDirection = EPP_ReactionMoveDirection::None;
	TransformSettings.RotationSettings.RotationDirection = EPP_ReactionRotationDirection::None;
}

UAbilitySystemComponent* UPP_PredictionComponent::GetAbilitySystemComponentFromActor(AActor* Actor)
{
	if (!Actor) return nullptr;

	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (APlayerState* PlayerState = Character->GetPlayerState<APlayerState>())
		{
			if (UAbilitySystemComponent* PlayerStateASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerState))
			{
				return PlayerStateASC;
			}
		}
	}

	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
}

void UPP_PredictionComponent::ApplyReactionTransform
(AActor* InstigatorActor, AActor* TargetActor,
 const FPP_ReactionTransformSettings& TransformSettings)
{
	if (!InstigatorActor || !TargetActor) return;

	// Rotate first so movement can use the intended facing and reference axes.
	const FPP_ReactionRotationSettings& RotationSettings = TransformSettings.RotationSettings;
	if (RotationSettings.RotationDirection != EPP_ReactionRotationDirection::None)
	{
		AActor* ReferenceActor = ResolveReactionReferenceActor(InstigatorActor, TargetActor,
		                                                       RotationSettings.ReferenceActorSource);

		ApplyReactionTransformToRecipients(InstigatorActor, TargetActor, ReferenceActor,
		                                   RotationSettings.Recipient,
		                                   [this, &RotationSettings](AActor* RecipientActor, AActor* InReferenceActor)
		                                   {
			                                   ApplyReactionRotation(RecipientActor, InReferenceActor,
			                                                         RotationSettings);
		                                   });
	}

	const FPP_ReactionMovementSettings& MovementSettings = TransformSettings.MovementSettings;
	if (MovementSettings.MoveDirection != EPP_ReactionMoveDirection::None)
	{
		AActor* ReferenceActor = ResolveReactionReferenceActor(InstigatorActor, TargetActor,
		                                                       MovementSettings.ReferenceActorSource);

		ApplyReactionTransformToRecipients(InstigatorActor, TargetActor, ReferenceActor,
		                                   MovementSettings.Recipient,
		                                   [this, &MovementSettings](AActor* RecipientActor, AActor* InReferenceActor)
		                                   {
			                                   ApplyReactionMovement(RecipientActor, InReferenceActor,
			                                                         MovementSettings);
		                                   });
	}
}

void UPP_PredictionComponent::ApplyReactionTransformToRecipients(AActor* InstigatorActor, AActor* TargetActor,
	AActor* ReferenceActor, EPP_ReactionTransformRecipient Recipient,
	TFunctionRef<void (AActor* RecipientActor, AActor* ReferenceActor)> ApplyFunction)
{
	if (!ReferenceActor) return;

	auto ApplyToRecipient = [ReferenceActor, &ApplyFunction](AActor* RecipientActor)
	{
		if (!RecipientActor || RecipientActor == ReferenceActor) return;
		ApplyFunction(RecipientActor, ReferenceActor);
	};

	switch (Recipient)
	{
	case EPP_ReactionTransformRecipient::Instigator:
		ApplyToRecipient(InstigatorActor);
		break;

	case EPP_ReactionTransformRecipient::Target:
		ApplyToRecipient(TargetActor);
		break;

	case EPP_ReactionTransformRecipient::Both:
		ApplyToRecipient(InstigatorActor);
		if (TargetActor != InstigatorActor)
		{
			ApplyToRecipient(TargetActor);
		}
		break;

	default:
		break;
	}
}

void UPP_PredictionComponent::ApplyReactionMovement(AActor* RecipientActor, AActor* ReferenceActor,
	const FPP_ReactionMovementSettings& MovementSettings)
{
	if (!RecipientActor || !ReferenceActor) return;

	if (MovementSettings.MoveDirection != EPP_ReactionMoveDirection::KeepCurrentDistance
		&& MovementSettings.MoveDistance <= 0.f)
	{
		return;
	}

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();

	FVector ReferenceForward = ReferenceActor->GetActorForwardVector();
	ReferenceForward.Z = 0.f;
	ReferenceForward = ReferenceForward.GetSafeNormal();
	if (ReferenceForward.IsNearlyZero()) return;

	FVector ReferenceRight = ReferenceActor->GetActorRightVector();
	ReferenceRight.Z = 0.f;
	ReferenceRight = ReferenceRight.GetSafeNormal();
	if (ReferenceRight.IsNearlyZero())
	{
		ReferenceRight = FVector::CrossProduct(FVector::UpVector, ReferenceForward).GetSafeNormal();
	}

	const FVector RelativeLocation = RecipientLocation - ReferenceLocation;
	const float CurrentForwardProjection = FVector::DotProduct(RelativeLocation, ReferenceForward);
	const float CurrentLateralProjection = FVector::DotProduct(RelativeLocation, ReferenceRight);

	float TargetForwardProjection = CurrentForwardProjection;
	float TargetLateralProjection = CurrentLateralProjection;

	switch (MovementSettings.MoveDirection)
	{
	case EPP_ReactionMoveDirection::KeepCurrentDistance:
		break;

	case EPP_ReactionMoveDirection::MoveCloser:
		TargetForwardProjection -= MovementSettings.MoveDistance;
		break;

	case EPP_ReactionMoveDirection::MoveAway:
		TargetForwardProjection += MovementSettings.MoveDistance;
		break;

	case EPP_ReactionMoveDirection::SnapToDistance:
		TargetForwardProjection = MovementSettings.MoveDistance;
		break;

	case EPP_ReactionMoveDirection::None:
	default:
		return;
	}

	switch (MovementSettings.LateralOffsetMode)
	{
	case EPP_ReactionLateralOffsetMode::KeepCurrent:
		break;

	case EPP_ReactionLateralOffsetMode::AddOffset:
		TargetLateralProjection += MovementSettings.LateralOffset;
		break;

	case EPP_ReactionLateralOffsetMode::SnapToOffset:
		TargetLateralProjection = MovementSettings.LateralOffset;
		break;

	default:
		break;
	}

	if (FMath::IsNearlyEqual(TargetForwardProjection, CurrentForwardProjection)
		&& FMath::IsNearlyEqual(TargetLateralProjection, CurrentLateralProjection))
	{
		return;
	}

	FVector NewLocation = ReferenceLocation
		+ (ReferenceForward * TargetForwardProjection)
		+ (ReferenceRight * TargetLateralProjection);
	NewLocation.Z = RecipientLocation.Z;

	SetReactionActorLocation(RecipientActor, NewLocation, MovementSettings);
}

void UPP_PredictionComponent::SetReactionActorLocation(AActor* RecipientActor, const FVector& TargetLocation,
	const FPP_ReactionMovementSettings& MovementSettings, bool bForceNoSweep)
{
	if (!RecipientActor) return;

	const TWeakObjectPtr<AActor> RecipientKey(RecipientActor);
	StopClientReactionMovementInterpolation(RecipientKey);

	UWorld* World = GetWorld();
	const APawn* RecipientPawn = Cast<APawn>(RecipientActor);
	ACharacter* RecipientCharacter = Cast<ACharacter>(RecipientActor);
	USkeletalMeshComponent* RecipientMesh = RecipientCharacter ? RecipientCharacter->GetMesh() : nullptr;
	const bool bCanInterpolateClient =
		World &&
		World->GetNetMode() != NM_DedicatedServer &&
		!RecipientActor->HasAuthority() &&
		MovementSettings.ClientInterpolationSpeed > KINDA_SMALL_NUMBER;
	const bool bInterpolateOwnedCapsule =
		bCanInterpolateClient && RecipientPawn && RecipientPawn->IsLocallyControlled();
	const bool bInterpolateProxyMesh =
		bCanInterpolateClient && RecipientPawn && !RecipientPawn->IsLocallyControlled() && RecipientMesh;
	const bool bSweep = !bForceNoSweep && MovementSettings.bSweep;
	const ETeleportType TeleportType = ToTeleportType(MovementSettings.TeleportType);


	if (!bInterpolateOwnedCapsule && !bInterpolateProxyMesh)
	{
		RecipientActor->SetActorLocation(TargetLocation, bSweep, nullptr, TeleportType);

		return;
	}

	constexpr float InterpolationInterval = 1.0f / 60.0f;
	const float InterpolationSpeed = MovementSettings.ClientInterpolationSpeed;
	const TWeakObjectPtr<AActor> WeakRecipient(RecipientActor);
	double LastUpdateTime = World->GetTimeSeconds();

	if (bInterpolateProxyMesh)
	{
		const FVector StartingMeshWorldLocation = RecipientMesh->GetComponentLocation();
		const FVector RestingRelativeLocation = RecipientMesh->GetRelativeLocation();
		UCharacterMovementComponent* CharacterMovement = RecipientCharacter->GetCharacterMovement();

		FPP_ProxyReactionVisualInterpolation VisualState;
		VisualState.Mesh = RecipientMesh;
		VisualState.MovementComponent = CharacterMovement;
		VisualState.RestingRelativeLocation = RestingRelativeLocation;
		if (CharacterMovement)
		{
			VisualState.SavedNetworkSmoothingMode = static_cast<uint8>(CharacterMovement->NetworkSmoothingMode);
			CharacterMovement->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}

		// Snap the replicated capsule to authority, then visually ease only the mesh.
		RecipientActor->SetActorLocation(TargetLocation, bSweep, nullptr, TeleportType);
		RecipientMesh->SetWorldLocation(StartingMeshWorldLocation, false, nullptr, ETeleportType::TeleportPhysics);

		ProxyReactionVisualInterpolations.Add(RecipientKey, VisualState);

		FTimerDelegate VisualInterpolationDelegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, WeakRecipient, InterpolationSpeed, LastUpdateTime]() mutable
			{
				UWorld* CurrentWorld = GetWorld();
				FPP_ProxyReactionVisualInterpolation* State =
					ProxyReactionVisualInterpolations.Find(WeakRecipient);
				USkeletalMeshComponent* Mesh = State ? State->Mesh.Get() : nullptr;
				if (!CurrentWorld || !Mesh)
				{
					StopClientReactionMovementInterpolation(WeakRecipient);
					return;
				}

				const double CurrentTime = CurrentWorld->GetTimeSeconds();
				const float DeltaSeconds = static_cast<float>(FMath::Max(0.0, CurrentTime - LastUpdateTime));
				LastUpdateTime = CurrentTime;
				const FVector NextRelativeLocation = FMath::VInterpConstantTo(
					Mesh->GetRelativeLocation(), State->RestingRelativeLocation,
					DeltaSeconds, InterpolationSpeed);
				Mesh->SetRelativeLocation(NextRelativeLocation, false, nullptr, ETeleportType::None);

				if (NextRelativeLocation.Equals(State->RestingRelativeLocation, 0.1f))
				{
					StopClientReactionMovementInterpolation(WeakRecipient);
				}
			});

		FTimerHandle& TimerHandle = ClientReactionMovementInterpolationTimers.FindOrAdd(RecipientKey);
		World->GetTimerManager().SetTimer(
			TimerHandle, VisualInterpolationDelegate, InterpolationInterval, true, 0.0f);
		return;
	}

	FTimerDelegate InterpolationDelegate = FTimerDelegate::CreateWeakLambda(
		this,
		[this, WeakRecipient, TargetLocation, InterpolationSpeed, bSweep, TeleportType,
			LastUpdateTime]() mutable
		{
			AActor* StrongRecipient = WeakRecipient.Get();
			UWorld* CurrentWorld = GetWorld();
			if (!StrongRecipient || !CurrentWorld)
			{
				StopClientReactionMovementInterpolation(WeakRecipient);
				return;
			}

			const double CurrentTime = CurrentWorld->GetTimeSeconds();
			const float DeltaSeconds = static_cast<float>(FMath::Max(0.0, CurrentTime - LastUpdateTime));
			LastUpdateTime = CurrentTime;

			const FVector CurrentLocation = StrongRecipient->GetActorLocation();
			const FVector NextLocation = FMath::VInterpConstantTo(
				CurrentLocation, TargetLocation, DeltaSeconds, InterpolationSpeed);
			const bool bReachedTarget = NextLocation.Equals(TargetLocation, 0.1f);

			FHitResult SweepHit;
			StrongRecipient->SetActorLocation(
				bReachedTarget ? TargetLocation : NextLocation,
				bSweep,
				&SweepHit,
				bReachedTarget ? TeleportType : ETeleportType::None);

			if (bReachedTarget || SweepHit.bBlockingHit)
			{
				StopClientReactionMovementInterpolation(WeakRecipient);
			}
		});

	FTimerHandle& TimerHandle = ClientReactionMovementInterpolationTimers.FindOrAdd(RecipientKey);
	World->GetTimerManager().SetTimer(
		TimerHandle, InterpolationDelegate, InterpolationInterval, true, 0.0f);
}

void UPP_PredictionComponent::StopClientReactionMovementInterpolation(TWeakObjectPtr<AActor> RecipientActor)
{
	if (FTimerHandle* TimerHandle = ClientReactionMovementInterpolationTimers.Find(RecipientActor))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(*TimerHandle);
		}
	}

	if (FPP_ProxyReactionVisualInterpolation* State = ProxyReactionVisualInterpolations.Find(RecipientActor))
	{
		if (USkeletalMeshComponent* Mesh = State->Mesh.Get())
		{

			Mesh->SetRelativeLocation(
				State->RestingRelativeLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}

		if (UCharacterMovementComponent* MovementComponent = State->MovementComponent.Get())
		{
			MovementComponent->NetworkSmoothingMode =
				static_cast<ENetworkSmoothingMode>(State->SavedNetworkSmoothingMode);
		}
	}

	ClientReactionMovementInterpolationTimers.Remove(RecipientActor);
	ProxyReactionVisualInterpolations.Remove(RecipientActor);
}

void UPP_PredictionComponent::StopAllClientReactionMovementInterpolations()
{
	TArray<TWeakObjectPtr<AActor>> Recipients;
	ClientReactionMovementInterpolationTimers.GetKeys(Recipients);
	for (const TWeakObjectPtr<AActor>& Recipient : Recipients)
	{
		StopClientReactionMovementInterpolation(Recipient);
	}

	ClientReactionMovementInterpolationTimers.Reset();
	ProxyReactionVisualInterpolations.Reset();
}

void UPP_PredictionComponent::ApplyReactionRotation
(AActor* RecipientActor, AActor* ReferenceActor,
 const FPP_ReactionRotationSettings& RotationSettings) const
{
	if (!RecipientActor || !ReferenceActor) return;

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();
	const FRotator BeforeRotation = RecipientActor->GetActorRotation();
	FRotator DesiredRotation = BeforeRotation;

	switch (RotationSettings.RotationDirection)
	{
	case EPP_ReactionRotationDirection::FaceReferenceActor:
		{
			FVector ToReference = ReferenceLocation - RecipientLocation;
			ToReference.Z = 0.f;
			if (const FVector FacingDirection = ToReference.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPP_ReactionRotationDirection::FaceAwayFromReference:
		{
			FVector AwayFromReference = RecipientLocation - ReferenceLocation;
			AwayFromReference.Z = 0.f;
			if (const FVector FacingDirection = AwayFromReference.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPP_ReactionRotationDirection::FaceOppositeReferenceForward:
		{
			FVector ReferenceForward = RotationSettings.bUseReferenceFacingOverride
				                           ? RotationSettings.ReferenceFacingOverride.Vector()
				                           : ReferenceActor->GetActorForwardVector();

			ReferenceForward.Z = 0.f;
			ReferenceForward = ReferenceForward.GetSafeNormal();
			if (ReferenceForward.IsNearlyZero())
			{
				break;
			}

			const FVector OppositeDirection = -ReferenceForward;
			if (const FVector FacingDirection = OppositeDirection.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPP_ReactionRotationDirection::FaceDirection:
		DesiredRotation = RotationSettings.DirectionToFace;
		break;

	case EPP_ReactionRotationDirection::None:
	default:
		return;
	}

	RecipientActor->SetActorRotation(DesiredRotation, ToTeleportType(RotationSettings.TeleportType));
}

AActor* UPP_PredictionComponent::ResolveReactionReferenceActor(AActor* InstigatorActor, AActor* TargetActor,
 EPP_ReactionReferenceActorSource ReferenceActorSource)
{
	switch (ReferenceActorSource)
	{
	case EPP_ReactionReferenceActorSource::Instigator:
		return InstigatorActor;

	case EPP_ReactionReferenceActorSource::Target:
		return TargetActor;

	default:
		return nullptr;
	}
}

ETeleportType UPP_PredictionComponent::ToTeleportType(EPP_ReactionTeleportType TeleportType)
{
	return TeleportType == EPP_ReactionTeleportType::ResetPhysics
		       ? ETeleportType::ResetPhysics
		       : ETeleportType::None;
}
