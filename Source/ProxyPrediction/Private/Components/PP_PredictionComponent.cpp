#include "Components/PP_PredictionComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"


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
	
	const FPMMO_ReactionPredictionContext Context = MakeReactionPredictionContext();
	
	AddPendingPredictedReaction(Context, TargetActor, ReactionTag);
	
	const float StartPosition = GetReactionStartPosition(Reaction);
	
	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	
	if (!bPlayed)
	{
		ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag);
		return false;
	}

	ServerConfirmPredictedReaction(Context, TargetActor, ReactionTag);

	return true;
}

void UPP_PredictionComponent::ServerConfirmPredictedReaction_Implementation(FPMMO_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || !OwnerActor->HasAuthority()) return;

	if (!Context.IsValid() || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

	PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	
	if (UWorld* World = GetWorld())
	{
		const float MontageLength = Reaction.Montage ? Reaction.Montage->GetPlayLength() : 0.f;
		const float PlayRate = FMath::Max(FMath::Abs(Reaction.PlayRate), KINDA_SMALL_NUMBER);
		const float RemainingDuration = FMath::Max(0.05f, (MontageLength - StartPosition) / PlayRate);
		TWeakObjectPtr<UPP_PredictionComponent> WeakThis(this);
		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		const FPMMO_ReactionPredictionContext CapturedContext = Context;
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

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation(FPMMO_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
	
}

void UPP_PredictionComponent::MulticastFinishConfirmedReaction_Implementation(FPMMO_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag, FVector ServerFinalLocation)
{
}

bool UPP_PredictionComponent::CanPlayPredictedReactionOnTargetProxy(AActor* TargetActor,
	const FPP_ReactionDataEntry& Reaction) const
{
}

FPMMO_ReactionPredictionContext UPP_PredictionComponent::MakeReactionPredictionContext()
{
}

void UPP_PredictionComponent::AddPendingPredictedReaction(const FPMMO_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
}

void UPP_PredictionComponent::RemoveExpiredPendingPredictedReactions()
{
}

float UPP_PredictionComponent::GetReactionStartPosition(const FPP_ReactionDataEntry& Reaction) const
{
}

bool UPP_PredictionComponent::PlayReactionMontageOnActor(AActor* TargetActor, const FPP_ReactionDataEntry& Reaction,
	float StartPosition, bool bForceRestart) const
{
}

bool UPP_PredictionComponent::ConsumePendingPredictedReaction(const FPMMO_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
}

void UPP_PredictionComponent::AddDeferredPredictedReactionCorrection(const FPMMO_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
}

bool UPP_PredictionComponent::ConsumeDeferredPredictedReactionCorrection(const FPMMO_ReactionPredictionContext& Context,
	AActor* TargetActor, FGameplayTag ReactionTag)
{
}

void UPP_PredictionComponent::RemoveExpiredDeferredPredictedReactionCorrections()
{
}

void UPP_PredictionComponent::ClientPlayOwnerConfirmedReaction_Implementation(FPMMO_ReactionPredictionContext Context,
	AActor* TargetActor, AActor* InstigatorActor, FGameplayTag ReactionTag)
{
}
