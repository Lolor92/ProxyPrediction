#include "Components/PP_PredictionComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

namespace
{
	const TCHAR* PP_NetModeToString(const UWorld* World)
	{
		if (!World) return TEXT("NoWorld");

		switch (World->GetNetMode())
		{
		case NM_Client:
			return TEXT("Client");
		case NM_DedicatedServer:
			return TEXT("DedicatedServer");
		case NM_ListenServer:
			return TEXT("ListenServer");
		case NM_Standalone:
			return TEXT("Standalone");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* PP_YesNo(const bool bValue)
	{
		return bValue ? TEXT("1") : TEXT("0");
	}

	void PP_SetMovementIgnore(AActor* MovingActor, AActor* IgnoredActor, bool bShouldIgnore)
	{
		if (!MovingActor || !IgnoredActor) return;

		UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(MovingActor->GetRootComponent());
		if (!RootPrimitive) return;

		RootPrimitive->IgnoreActorWhenMoving(IgnoredActor, bShouldIgnore);
	}
}

UPP_PredictionComponent::UPP_PredictionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UPP_PredictionComponent::PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag)
{
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PredictRequest Net=%s Owner=%s Target=%s Tag=%s"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(GetOwner()), *GetNameSafe(TargetActor), *ReactionTag.ToString());

	if (!ReactionData || !ReactionTag.IsValid()) return false;
	
	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return false;
	
	if (!CanPlayPredictedReactionOnTargetProxy(TargetActor, Reaction)) return false;
	
	const FPP_ReactionPredictionContext Context = MakeReactionPredictionContext();
	
	AddPendingPredictedReaction(Context, TargetActor, ReactionTag);
	AddPredictedReactionCollisionIgnore(TargetActor);
	
	const float StartPosition = GetReactionStartPosition(Reaction);
	
	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PredictLocalPlay Net=%s Owner=%s Target=%s Tag=%s PredictionId=%d Played=%s Start=%.3f"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(GetOwner()), *GetNameSafe(TargetActor), *ReactionTag.ToString(),
		Context.PredictionId, PP_YesNo(bPlayed), StartPosition);
	
	if (!bPlayed)
	{
		RemovePredictedReactionCollisionIgnore(TargetActor);
		ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION SendServerConfirm Net=%s Owner=%s Target=%s Tag=%s PredictionId=%d"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(GetOwner()), *GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);

	ServerConfirmPredictedReaction(Context, TargetActor, ReactionTag);

	return true;
}

void UPP_PredictionComponent::ServerConfirmPredictedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ServerConfirm ENTER Net=%s Owner=%s Target=%s Tag=%s PredictionId=%d Auth=%s"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *ReactionTag.ToString(),
		Context.PredictionId, PP_YesNo(OwnerActor && OwnerActor->HasAuthority()));

	if (!OwnerActor || !OwnerActor->HasAuthority()) return;

	if (!Context.IsValid() || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

	const bool bServerPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ServerPlay Target=%s Tag=%s PredictionId=%d Played=%s Start=%.3f"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, PP_YesNo(bServerPlayed), StartPosition);
	
	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);
		TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FPP_ReactionPredictionContext CapturedContext = Context;
		const FGameplayTag CapturedReactionTag = ReactionTag;

		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ServerFinishTimer Target=%s Tag=%s PredictionId=%d Remaining=%.3f"),
			*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, RemainingDuration);

		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(TimerHandle,[WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
			{
				UPP_PredictionComponent* StrongThis = WeakThis.Get();
				AActor* StrongTarget = WeakTarget.Get();
				if (!StrongThis || !StrongTarget) return;

				const FVector ServerFinalLocation = StrongTarget->GetActorLocation();
				UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ServerFinishFire Target=%s Tag=%s PredictionId=%d ServerLoc=%s"),
					*GetNameSafe(StrongTarget), *CapturedReactionTag.ToString(), CapturedContext.PredictionId, *ServerFinalLocation.ToString());
				StrongThis->MulticastFinishConfirmedReaction(CapturedContext, StrongTarget,
					CapturedReactionTag, ServerFinalLocation);
			},
			RemainingDuration,
			false);
	}
	
	if (UPP_PredictionComponent* TargetPredictionComponent =
	TargetActor->FindComponentByClass<UPP_PredictionComponent>())
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION SendOwnerClientRPC Target=%s Instigator=%s Tag=%s PredictionId=%d"),
			*GetNameSafe(TargetActor), *GetNameSafe(OwnerActor), *ReactionTag.ToString(), Context.PredictionId);
		TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(Context, TargetActor, OwnerActor, ReactionTag);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION SendMulticastPlay Target=%s Tag=%s PredictionId=%d"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);
	MulticastPlayConfirmedReaction(Context, TargetActor, ReactionTag);
}

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastPlay ENTER Net=%s Owner=%s Target=%s Tag=%s PredictionId=%d OwnerAuth=%s"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *ReactionTag.ToString(),
		Context.PredictionId, PP_YesNo(OwnerActor && OwnerActor->HasAuthority()));

	if (!OwnerActor || OwnerActor->HasAuthority()) return;

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	if (ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag))
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastPlay ConsumedPending_DeferFinalCorrection Owner=%s Target=%s Tag=%s PredictionId=%d"),
			*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);
		AddDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
		
		return;
	}
	
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastPlay TargetLocalCheck Owner=%s Target=%s LocalTarget=%s"),
		*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), PP_YesNo(bTargetLocallyControlled));
	if (bTargetLocallyControlled) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastPlay Played Owner=%s Target=%s Tag=%s PredictionId=%d Played=%s Start=%.3f"),
		*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, PP_YesNo(bPlayed), StartPosition);
}

bool UPP_PredictionComponent::ConsumePendingPredictedReaction(const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	if (!Context.IsValid() || !TargetActor || !ReactionTag.IsValid()) return false;

	RemoveExpiredPendingPredictedReactions();

	for (int32 Index = PendingPredictedReactions.Num() - 1; Index >= 0; --Index)
	{
		const FPP_PendingPredictedReaction& Entry = PendingPredictedReactions[Index];

		if (Entry.TargetActor.Get() == TargetActor && Entry.ReactionTag == ReactionTag && Entry.PredictionId == Context.PredictionId)
		{
			PendingPredictedReactions.RemoveAtSwap(Index);
			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ConsumePending HIT Target=%s Tag=%s PredictionId=%d"),
				*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ConsumePending MISS Target=%s Tag=%s PredictionId=%d PendingCount=%d"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, PendingPredictedReactions.Num());
	return false;
}

void UPP_PredictionComponent::ClientPlayOwnerConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, AActor* InstigatorActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC ENTER Net=%s Owner=%s Target=%s Instigator=%s Tag=%s PredictionId=%d"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *GetNameSafe(InstigatorActor),
		*ReactionTag.ToString(), Context.PredictionId);

	if (!OwnerActor || OwnerActor != TargetActor) return;

	APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return;

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (!TargetCharacter) return;

	UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();
	const bool bPreviousIgnoreClientErrorChecks = MovementComponent
		? MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection
		: false;
	const bool bPreviousClientIgnoreMovementCorrections = MovementComponent
		? MovementComponent->bClientIgnoreMovementCorrections
		: false;

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC SuppressCorrection Target=%s Tag=%s PredictionId=%d PrevErrorChecks=%s PrevClientIgnore=%s MovementValid=%s Loc=%s"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId,
		PP_YesNo(bPreviousIgnoreClientErrorChecks), PP_YesNo(bPreviousClientIgnoreMovementCorrections),
		PP_YesNo(MovementComponent != nullptr), *TargetActor->GetActorLocation().ToString());

	if (MovementComponent)
	{
		MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = true;
		MovementComponent->bClientIgnoreMovementCorrections = true;
	}

	const float StartPosition = GetReactionStartPosition(Reaction);

	// The owning victim starts this montage later than the server because this RPC arrives after latency.
	// While the local reaction plays, ignore both newly generated server corrections and already in-flight
	// corrections on this client. Normal reconciliation resumes when the local montage window is finished.
	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC LocalPlay Target=%s Tag=%s PredictionId=%d Played=%s Start=%.3f"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, PP_YesNo(bPlayed), StartPosition);

	if (!bPlayed)
	{
		if (MovementComponent)
		{
			MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bPreviousIgnoreClientErrorChecks;
			MovementComponent->bClientIgnoreMovementCorrections = bPreviousClientIgnoreMovementCorrections;
			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC RestoreAfterFailedPlay Target=%s Tag=%s PredictionId=%d RestoredErrorChecks=%s RestoredClientIgnore=%s"),
				*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId,
				PP_YesNo(bPreviousIgnoreClientErrorChecks), PP_YesNo(bPreviousClientIgnoreMovementCorrections));
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);

		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC RestoreTimer Target=%s Tag=%s PredictionId=%d Remaining=%.3f MontageLength=%.3f PlayRate=%.3f"),
			*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, RemainingDuration, MontageLength, Reaction.PlayRate);

		TWeakObjectPtr<UCharacterMovementComponent> WeakMovementComponent(MovementComponent);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FGameplayTag CapturedReactionTag = ReactionTag;
		const FPP_ReactionPredictionContext CapturedContext = Context;

		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(
			TimerHandle,
			[WeakMovementComponent, WeakTarget, CapturedReactionTag, CapturedContext,
				bPreviousIgnoreClientErrorChecks, bPreviousClientIgnoreMovementCorrections]()
			{
				if (UCharacterMovementComponent* StrongMovementComponent = WeakMovementComponent.Get())
				{
					StrongMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bPreviousIgnoreClientErrorChecks;
					StrongMovementComponent->bClientIgnoreMovementCorrections = bPreviousClientIgnoreMovementCorrections;
					UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC RestoreTimerFire Target=%s Tag=%s PredictionId=%d RestoredErrorChecks=%s RestoredClientIgnore=%s Loc=%s"),
						*GetNameSafe(WeakTarget.Get()), *CapturedReactionTag.ToString(), CapturedContext.PredictionId,
						PP_YesNo(bPreviousIgnoreClientErrorChecks), PP_YesNo(bPreviousClientIgnoreMovementCorrections),
						WeakTarget.IsValid() ? *WeakTarget->GetActorLocation().ToString() : TEXT("Invalid"));
				}
			},
			RemainingDuration,
			false);
	}
	else if (MovementComponent)
	{
		MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bPreviousIgnoreClientErrorChecks;
		MovementComponent->bClientIgnoreMovementCorrections = bPreviousClientIgnoreMovementCorrections;
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION OwnerClientRPC RestoreNoWorld Target=%s Tag=%s PredictionId=%d RestoredErrorChecks=%s RestoredClientIgnore=%s"),
			*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId,
			PP_YesNo(bPreviousIgnoreClientErrorChecks), PP_YesNo(bPreviousClientIgnoreMovementCorrections));
	}
}

void UPP_PredictionComponent::MulticastFinishConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag, FVector ServerFinalLocation)
{
	AActor* OwnerActor = GetOwner();
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastFinish ENTER Net=%s Owner=%s Target=%s Tag=%s PredictionId=%d OwnerAuth=%s ServerLoc=%s"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *ReactionTag.ToString(),
		Context.PredictionId, PP_YesNo(OwnerActor && OwnerActor->HasAuthority()), *ServerFinalLocation.ToString());

	if (!OwnerActor || OwnerActor->HasAuthority()) return;

	if (!TargetActor || !ReactionTag.IsValid()) return;

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();
	if (bTargetLocallyControlled)
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastFinish SkipLocalTarget Target=%s Tag=%s PredictionId=%d"),
			*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);
		return;
	}

	if (!ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag))
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastFinish NoDeferredCorrection Target=%s Tag=%s PredictionId=%d"),
			*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);
		RemovePredictedReactionCollisionIgnore(TargetActor);
		return;
	}

	RemovePredictedReactionCollisionIgnore(TargetActor);

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FVector Delta = ServerFinalLocation - ClientFinalLocation;
	const float Distance = Delta.Size();

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastFinish CorrectionCheck Target=%s Tag=%s PredictionId=%d ClientLoc=%s ServerLoc=%s Distance=%.3f Tol=%.3f Apply=%s MaxInstant=%.3f"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, *ClientFinalLocation.ToString(),
		*ServerFinalLocation.ToString(), Distance, FinalCorrectionTolerance, PP_YesNo(bApplyInstantFinalCorrection), MaxInstantFinalCorrectionDistance);

	if (Distance <= FinalCorrectionTolerance) return;

	if (!bApplyInstantFinalCorrection) return;

	if (Distance > MaxInstantFinalCorrectionDistance) return;

	TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION MulticastFinish AppliedInstantCorrection Target=%s Tag=%s PredictionId=%d NewLoc=%s"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, *TargetActor->GetActorLocation().ToString());
}

void UPP_PredictionComponent::AddDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context,
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

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION AddDeferredCorrection Target=%s Tag=%s PredictionId=%d Count=%d"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, DeferredPredictedReactionCorrections.Num());
}

bool UPP_PredictionComponent::ConsumeDeferredPredictedReactionCorrection(const FPP_ReactionPredictionContext& Context,
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
			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ConsumeDeferred HIT Target=%s Tag=%s PredictionId=%d"),
				*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION ConsumeDeferred MISS Target=%s Tag=%s PredictionId=%d DeferredCount=%d"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, DeferredPredictedReactionCorrections.Num());
	return false;
}

void UPP_PredictionComponent::RemoveExpiredDeferredPredictedReactionCorrections()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		DeferredPredictedReactionCorrections.Reset();
		ClearPredictedReactionCollisionIgnores();
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
			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION DeferredExpired Target=%s Tag=%s PredictionId=%d Expired=%s Invalid=%s"),
				*GetNameSafe(Entry.TargetActor.Get()), *Entry.ReactionTag.ToString(), Entry.PredictionId,
				PP_YesNo(bExpired), PP_YesNo(bInvalid));
			RemovePredictedReactionCollisionIgnore(Entry.TargetActor.Get());
			DeferredPredictedReactionCorrections.RemoveAtSwap(Index);
		}
	}
}

void UPP_PredictionComponent::AddPredictedReactionCollisionIgnore(AActor* TargetActor)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !TargetActor || OwnerActor == TargetActor) return;

	int32& Count = PredictedReactionCollisionIgnoreCounts.FindOrAdd(TargetActor);
	++Count;

	if (Count == 1)
	{
		PP_SetMovementIgnore(OwnerActor, TargetActor, true);
		PP_SetMovementIgnore(TargetActor, OwnerActor, true);

		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CollisionIgnoreAdd Owner=%s Target=%s Count=%d"),
			*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), Count);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CollisionIgnoreIncrement Owner=%s Target=%s Count=%d"),
		*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), Count);
}

void UPP_PredictionComponent::RemovePredictedReactionCollisionIgnore(AActor* TargetActor)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !TargetActor || OwnerActor == TargetActor) return;

	int32* CountPtr = PredictedReactionCollisionIgnoreCounts.Find(TargetActor);
	if (!CountPtr) return;

	if (*CountPtr > 1)
	{
		--(*CountPtr);
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CollisionIgnoreDecrement Owner=%s Target=%s Count=%d"),
			*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), *CountPtr);
		return;
	}

	PredictedReactionCollisionIgnoreCounts.Remove(TargetActor);
	PP_SetMovementIgnore(OwnerActor, TargetActor, false);
	PP_SetMovementIgnore(TargetActor, OwnerActor, false);

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CollisionIgnoreRestored Owner=%s Target=%s Count=0"),
		*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor));
}

void UPP_PredictionComponent::ClearPredictedReactionCollisionIgnores()
{
	AActor* OwnerActor = GetOwner();

	for (auto It = PredictedReactionCollisionIgnoreCounts.CreateIterator(); It; ++It)
	{
		AActor* TargetActor = It.Key().Get();
		if (OwnerActor && TargetActor && OwnerActor != TargetActor)
		{
			PP_SetMovementIgnore(OwnerActor, TargetActor, false);
			PP_SetMovementIgnore(TargetActor, OwnerActor, false);

			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CollisionIgnoreCleared Owner=%s Target=%s Count=0"),
				*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor));
		}

		It.RemoveCurrent();
	}
}

bool UPP_PredictionComponent::CanPlayPredictedReactionOnTargetProxy(AActor* TargetActor,
	const FPP_ReactionDataEntry& Reaction) const
{
	if (!TargetActor || !Reaction.Montage) return false;

	const UWorld* World = GetWorld();
	if (!World) return false;

	if (World->GetNetMode() == NM_DedicatedServer) return false;

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return false;

	// This component is on the attacker.
	// Only the attacking client should predict target proxy reaction.
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
			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CanPredict RejectMinReplay Owner=%s Target=%s Delta=%.3f Min=%.3f"),
				*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor), Now - *LastTime, Reaction.MinReplayInterval);
			return false;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION CanPredict OK Owner=%s Target=%s"),
		*GetNameSafe(OwnerActor), *GetNameSafe(TargetActor));
	return true;
}

FPP_ReactionPredictionContext UPP_PredictionComponent::MakeReactionPredictionContext()
{
	FPP_ReactionPredictionContext Context;
	Context.PredictionId = NextPredictionId;

	NextPredictionId = (NextPredictionId + 1) % MaxPredictionId;
	if (NextPredictionId == INDEX_NONE)
	{
		NextPredictionId = 0;
	}

	return Context;
}

void UPP_PredictionComponent::AddPendingPredictedReaction(const FPP_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
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

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION AddPending Target=%s Tag=%s PredictionId=%d Count=%d"),
		*GetNameSafe(TargetActor), *ReactionTag.ToString(), Context.PredictionId, PendingPredictedReactions.Num());
	
	if (PendingPredictedReactions.Num() > MaxPendingPredictedReactions)
	{
		const int32 NumToRemove = PendingPredictedReactions.Num() - MaxPendingPredictedReactions;
		for (int32 RemoveIndex = 0; RemoveIndex < NumToRemove; ++RemoveIndex)
		{
			RemovePredictedReactionCollisionIgnore(PendingPredictedReactions[RemoveIndex].TargetActor.Get());
		}
		PendingPredictedReactions.RemoveAt(0, NumToRemove, EAllowShrinking::No);
	}
}

void UPP_PredictionComponent::RemoveExpiredPendingPredictedReactions()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		PendingPredictedReactions.Reset();
		ClearPredictedReactionCollisionIgnores();
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
			UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PendingExpired Target=%s Tag=%s PredictionId=%d Expired=%s Invalid=%s"),
				*GetNameSafe(Entry.TargetActor.Get()), *Entry.ReactionTag.ToString(), Entry.PredictionId,
				PP_YesNo(bExpired), PP_YesNo(bInvalid));
			RemovePredictedReactionCollisionIgnore(Entry.TargetActor.Get());
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

bool UPP_PredictionComponent::PlayReactionMontageOnActor(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction,
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
	const float PreviousPosition = bWasPlaying ? AnimInstance->Montage_GetPosition(Reaction.Montage) : -1.f;

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PlayMontage ENTER Net=%s Owner=%s Target=%s Montage=%s ForceRestart=%s WasPlaying=%s PrevPos=%.3f Start=%.3f PlayRate=%.3f Loc=%s"),
		PP_NetModeToString(GetWorld()), *GetNameSafe(GetOwner()), *GetNameSafe(TargetActor), *GetNameSafe(Reaction.Montage),
		PP_YesNo(bForceRestart), PP_YesNo(bWasPlaying), PreviousPosition, StartPosition, Reaction.PlayRate,
		*TargetActor->GetActorLocation().ToString());

	if (!bForceRestart && bWasPlaying)
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PlayMontage AlreadyPlaying_NoRestart Target=%s Montage=%s Pos=%.3f"),
			*GetNameSafe(TargetActor), *GetNameSafe(Reaction.Montage), PreviousPosition);
		return true;
	}
	
	if (bForceRestart && bWasPlaying)
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PlayMontage STOP_FOR_RESTART Target=%s Montage=%s Pos=%.3f"),
			*GetNameSafe(TargetActor), *GetNameSafe(Reaction.Montage), PreviousPosition);
		AnimInstance->Montage_Stop(0.f, Reaction.Montage);
	}

	const float PlayedLength = AnimInstance->Montage_Play(Reaction.Montage, Reaction.PlayRate);

	if (PlayedLength <= 0.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PlayMontage FAILED Target=%s Montage=%s PlayedLength=%.3f"),
			*GetNameSafe(TargetActor), *GetNameSafe(Reaction.Montage), PlayedLength);
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

	UE_LOG(LogTemp, Warning, TEXT("PP_REACTION PlayMontage SUCCESS Target=%s Montage=%s FinalPos=%.3f MontageLength=%.3f"),
		*GetNameSafe(TargetActor), *GetNameSafe(Reaction.Montage), AnimInstance->Montage_GetPosition(Reaction.Montage), MontageLength);

	return true;
}
