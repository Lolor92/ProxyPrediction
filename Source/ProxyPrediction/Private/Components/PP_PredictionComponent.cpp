#include "Components/PP_PredictionComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
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

	void PP_SetIgnoreActorWhenMoving(UPrimitiveComponent* Component, AActor* IgnoredActor, bool bShouldIgnore)
	{
		if (!Component || !IgnoredActor) return;

		Component->IgnoreActorWhenMoving(IgnoredActor, bShouldIgnore);
	}

	void PP_SetMovementIgnore(AActor* MovingActor, AActor* IgnoredActor, bool bShouldIgnore)
	{
		if (!MovingActor || !IgnoredActor) return;

		if (ACharacter* MovingCharacter = Cast<ACharacter>(MovingActor))
		{
			PP_SetIgnoreActorWhenMoving(MovingCharacter->GetCapsuleComponent(), IgnoredActor, bShouldIgnore);

			if (UCharacterMovementComponent* MovementComponent = MovingCharacter->GetCharacterMovement())
			{
				PP_SetIgnoreActorWhenMoving(Cast<UPrimitiveComponent>(MovementComponent->UpdatedComponent), IgnoredActor, bShouldIgnore);
			}
		}

		PP_SetIgnoreActorWhenMoving(Cast<UPrimitiveComponent>(MovingActor->GetRootComponent()), IgnoredActor, bShouldIgnore);
	}
}

UPP_PredictionComponent::UPP_PredictionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UPP_PredictionComponent::PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag)
{

	if (!ReactionData || !ReactionTag.IsValid()) return false;
	
	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return false;
	
	if (!CanPlayPredictedReactionOnTargetProxy(TargetActor, Reaction)) return false;
	
	const FPP_ReactionPredictionContext Context = MakeReactionPredictionContext();
	
	AddPendingPredictedReaction(Context, TargetActor, ReactionTag);
	AddPredictedReactionCollisionIgnore(TargetActor);
	
	const float StartPosition = GetReactionStartPosition(Reaction);
	
	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

	
	if (!bPlayed)
	{
		RemovePredictedReactionCollisionIgnore(TargetActor);
		ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag);
		return false;
	}


	ServerConfirmPredictedReaction(Context, TargetActor, ReactionTag);

	return true;
}

void UPP_PredictionComponent::ServerConfirmPredictedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();


	if (!OwnerActor || !OwnerActor->HasAuthority()) return;

	if (!Context.IsValid() || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

	const bool bServerPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	
	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);
		TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FPP_ReactionPredictionContext CapturedContext = Context;
		const FGameplayTag CapturedReactionTag = ReactionTag;


		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(TimerHandle,[WeakThis, WeakTarget, CapturedContext, CapturedReactionTag]()
			{
				UPP_PredictionComponent* StrongThis = WeakThis.Get();
				AActor* StrongTarget = WeakTarget.Get();
				if (!StrongThis || !StrongTarget) return;

				const FVector ServerFinalLocation = StrongTarget->GetActorLocation();
				StrongThis->MulticastFinishConfirmedReaction(CapturedContext, StrongTarget,
					CapturedReactionTag, ServerFinalLocation);
			},
			RemainingDuration,
			false);
	}
	
	if (UPP_PredictionComponent* TargetPredictionComponent =
	TargetActor->FindComponentByClass<UPP_PredictionComponent>())
	{
		TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(Context, TargetActor, OwnerActor, ReactionTag);
	}
	
	MulticastPlayConfirmedReaction(Context, TargetActor, ReactionTag);
}

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || OwnerActor->HasAuthority()) return;

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	if (ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag))
	{
		AddDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
		
		return;
	}
	
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();
	if (bTargetLocallyControlled) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
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
			return true;
		}
	}

	return false;
}

void UPP_PredictionComponent::ClientPlayOwnerConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, AActor* InstigatorActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();

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


	if (!bPlayed)
	{
		if (MovementComponent)
		{
			MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bPreviousIgnoreClientErrorChecks;
			MovementComponent->bClientIgnoreMovementCorrections = bPreviousClientIgnoreMovementCorrections;
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);


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
				}
			},
			RemainingDuration,
			false);
	}
	else if (MovementComponent)
	{
		MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bPreviousIgnoreClientErrorChecks;
		MovementComponent->bClientIgnoreMovementCorrections = bPreviousClientIgnoreMovementCorrections;
	}
}

void UPP_PredictionComponent::MulticastFinishConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag, FVector ServerFinalLocation)
{
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || OwnerActor->HasAuthority()) return;

	if (!TargetActor || !ReactionTag.IsValid()) return;

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();
	if (bTargetLocallyControlled)
	{
		return;
	}

	if (!ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag))
	{
		RemovePredictedReactionCollisionIgnore(TargetActor);
		return;
	}

	RemovePredictedReactionCollisionIgnore(TargetActor);

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FVector Delta = ServerFinalLocation - ClientFinalLocation;
	const float Distance = Delta.Size();


	if (Distance <= FinalCorrectionTolerance) return;

	if (!bApplyInstantFinalCorrection) return;

	if (Distance > MaxInstantFinalCorrectionDistance) return;

	TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);
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

		return;
	}

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
		return;
	}

	PredictedReactionCollisionIgnoreCounts.Remove(TargetActor);
	PP_SetMovementIgnore(OwnerActor, TargetActor, false);
	PP_SetMovementIgnore(TargetActor, OwnerActor, false);

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
			return false;
		}
	}

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
