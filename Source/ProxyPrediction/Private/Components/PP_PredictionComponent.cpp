#include "Components/PP_PredictionComponent.h"
#include "TimerManager.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"

namespace
{
static FString PP_NetModeToString(const UWorld* World)
{
if (!World) return TEXT("NoWorld");

switch (World->GetNetMode())
{
case NM_Standalone: return TEXT("Standalone");
case NM_DedicatedServer: return TEXT("DedicatedServer");
case NM_ListenServer: return TEXT("ListenServer");
case NM_Client: return TEXT("Client");
default: return TEXT("UnknownNetMode");
}
}

static FString PP_RoleToString(const AActor* Actor)
{
if (!Actor) return TEXT("NoActor");

return FString::Printf(TEXT("LocalRole=%d RemoteRole=%d Auth=%d"),
static_cast<int32>(Actor->GetLocalRole()),
static_cast<int32>(Actor->GetRemoteRole()),
Actor->HasAuthority() ? 1 : 0);
}

static FString PP_PawnFlagsToString(const AActor* Actor)
{
const APawn* Pawn = Cast<APawn>(Actor);
return FString::Printf(TEXT("Pawn=%d LocalCtrl=%d"),
Pawn ? 1 : 0,
(Pawn && Pawn->IsLocallyControlled()) ? 1 : 0);
}

static FString PP_VecToString(const FVector& Vec)
{
return FString::Printf(TEXT("(%.2f %.2f %.2f)"), Vec.X, Vec.Y, Vec.Z);
}

static FString PP_RotToString(const FRotator& Rot)
{
return FString::Printf(TEXT("(P%.2f Y%.2f R%.2f)"), Rot.Pitch, Rot.Yaw, Rot.Roll);
}

static void PP_LogActorState(const TCHAR* Label, const UWorld* World, const AActor* Actor)
{
const ACharacter* Character = Cast<ACharacter>(Actor);
const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_STATE %s Time=%.3f Net=%s Actor=%s %s %s Loc=%s Rot=%s Vel=%s MoveMode=%d IgnoreErr=%d IgnoreCorr=%d RepMove=%d"),
Label,
World ? World->GetTimeSeconds() : -1.f,
*PP_NetModeToString(World),
*GetNameSafe(Actor),
*PP_RoleToString(Actor),
*PP_PawnFlagsToString(Actor),
Actor ? *PP_VecToString(Actor->GetActorLocation()) : TEXT("(null)"),
Actor ? *PP_RotToString(Actor->GetActorRotation()) : TEXT("(null)"),
Move ? *PP_VecToString(Move->Velocity) : TEXT("(no move)"),
Move ? static_cast<int32>(Move->MovementMode) : -1,
Move && Move->bIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0,
Move && Move->bClientIgnoreMovementCorrections ? 1 : 0,
Actor && Actor->IsReplicatingMovement() ? 1 : 0);
}
}


UPP_PredictionComponent::UPP_PredictionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UPP_PredictionComponent::PlayPredictedReactionOnTargetProxy(AActor* TargetActor, FGameplayTag ReactionTag,
	FPP_ReactionTransformSettings TransformSettings)
{

	if (!ReactionData || !ReactionTag.IsValid()) return false;
	
	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return false;
	
	if (!CanPlayPredictedReactionOnTargetProxy(TargetActor, Reaction)) return false;
const FPP_ReactionPredictionContext Context = MakeReactionPredictionContext();

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_PREDICT_BEGIN Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(GetOwner()),
*GetNameSafe(TargetActor),
*ReactionTag.ToString());
PP_LogActorState(TEXT("ProxyPredict_Owner_Before"), GetWorld(), GetOwner());
PP_LogActorState(TEXT("ProxyPredict_Target_Before"), GetWorld(), TargetActor);

AddPendingPredictedReaction(Context, TargetActor, ReactionTag);

const float StartPosition = GetReactionStartPosition(Reaction);

ApplyReactionTransform(GetOwner(), TargetActor, TransformSettings);

PP_LogActorState(TEXT("ProxyPredict_Owner_AfterTransform"), GetWorld(), GetOwner());
PP_LogActorState(TEXT("ProxyPredict_Target_AfterTransform"), GetWorld(), TargetActor);

const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_PREDICT_MONTAGE Time=%.3f Ctx=%d Played=%d StartPos=%.3f Montage=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
bPlayed ? 1 : 0,
StartPosition,
*GetNameSafe(Reaction.Montage));
if (!bPlayed)
	{
		ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag);
		return false;
	}

	ServerConfirmPredictedReaction(Context, TargetActor, ReactionTag, TransformSettings);

	return true;
}

void UPP_PredictionComponent::ServerConfirmPredictedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings)
{
	AActor* OwnerActor = GetOwner();


	if (!OwnerActor || !OwnerActor->HasAuthority()) return;

	if (!Context.IsValid() || !TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	FPP_ReactionDataEntry Reaction;
	if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

	const float StartPosition = GetReactionStartPosition(Reaction);

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_SERVER_CONFIRM_BEGIN Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString());
PP_LogActorState(TEXT("ServerConfirm_Owner_Before"), GetWorld(), OwnerActor);
PP_LogActorState(TEXT("ServerConfirm_Target_Before"), GetWorld(), TargetActor);

ApplyReactionTransform(OwnerActor, TargetActor, TransformSettings);

PP_LogActorState(TEXT("ServerConfirm_Owner_AfterTransform"), GetWorld(), OwnerActor);
PP_LogActorState(TEXT("ServerConfirm_Target_AfterTransform"), GetWorld(), TargetActor);

ApplyTargetEffects(OwnerActor, TargetActor, Reaction);

const bool bServerPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_SERVER_CONFIRM_MONTAGE Time=%.3f Ctx=%d Played=%d StartPos=%.3f Montage=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
bServerPlayed ? 1 : 0,
StartPosition,
*GetNameSafe(Reaction.Montage));
	
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
				const FRotator ServerFinalRotation = StrongTarget->GetActorRotation();
				StrongThis->MulticastFinishConfirmedReaction(CapturedContext, StrongTarget,
					CapturedReactionTag, ServerFinalLocation, ServerFinalRotation);
			},
			RemainingDuration,
			false);
	}
	
	if (UPP_PredictionComponent* TargetPredictionComponent =
	TargetActor->FindComponentByClass<UPP_PredictionComponent>())
	{
		TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(Context, TargetActor, OwnerActor, ReactionTag,
			TransformSettings);
	}
	
	MulticastPlayConfirmedReaction(Context, TargetActor, ReactionTag, TransformSettings);
}

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
	AActor* TargetActor, FGameplayTag ReactionTag, FPP_ReactionTransformSettings TransformSettings)
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

	ApplyReactionTransform(OwnerActor, TargetActor, TransformSettings);

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
	AActor* TargetActor, AActor* InstigatorActor, FGameplayTag ReactionTag,
	FPP_ReactionTransformSettings TransformSettings)
{
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || OwnerActor != TargetActor) return;

	APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return;

	if (!TargetActor || !ReactionData || !ReactionTag.IsValid()) return;

	FPP_ReactionDataEntry Reaction;
if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_CLIENT_BEGIN Time=%.3f Ctx=%d Owner=%s Target=%s Instigator=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*GetNameSafe(InstigatorActor),
*ReactionTag.ToString());
PP_LogActorState(TEXT("OwnerClient_Target_Before"), GetWorld(), TargetActor);
PP_LogActorState(TEXT("OwnerClient_Instigator_Before"), GetWorld(), InstigatorActor);

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
	ApplyReactionTransform(InstigatorActor, TargetActor, TransformSettings);

PP_LogActorState(TEXT("OwnerClient_Target_AfterTransform"), GetWorld(), TargetActor);
PP_LogActorState(TEXT("OwnerClient_Instigator_AfterTransform"), GetWorld(), InstigatorActor);

const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_CLIENT_MONTAGE Time=%.3f Ctx=%d Played=%d StartPos=%.3f Montage=%s PrevIgnoreErr=%d PrevIgnoreCorr=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
bPlayed ? 1 : 0,
StartPosition,
*GetNameSafe(Reaction.Montage),
bPreviousIgnoreClientErrorChecks ? 1 : 0,
bPreviousClientIgnoreMovementCorrections ? 1 : 0);
	
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
AActor* StrongTargetActor = WeakTarget.Get();

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_CLIENT_RESTORE_CORRECTIONS Time=%.3f Ctx=%d Target=%s Tag=%s RestoreIgnoreErr=%d RestoreIgnoreCorr=%d"),
StrongTargetActor && StrongTargetActor->GetWorld() ? StrongTargetActor->GetWorld()->GetTimeSeconds() : -1.f,
CapturedContext.PredictionId,
*GetNameSafe(StrongTargetActor),
*CapturedReactionTag.ToString(),
bPreviousIgnoreClientErrorChecks ? 1 : 0,
bPreviousClientIgnoreMovementCorrections ? 1 : 0);
PP_LogActorState(TEXT("OwnerClient_Target_BeforeRestoreCorrections"),
StrongTargetActor ? StrongTargetActor->GetWorld() : nullptr,
StrongTargetActor);

StrongMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = bPreviousIgnoreClientErrorChecks;
StrongMovementComponent->bClientIgnoreMovementCorrections = bPreviousClientIgnoreMovementCorrections;

PP_LogActorState(TEXT("OwnerClient_Target_AfterRestoreCorrections"),
StrongTargetActor ? StrongTargetActor->GetWorld() : nullptr,
StrongTargetActor);
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
	AActor* TargetActor, FGameplayTag ReactionTag, FVector ServerFinalLocation, FRotator ServerFinalRotation)
{
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor || OwnerActor->HasAuthority()) return;

	if (!TargetActor || !ReactionTag.IsValid()) return;

	const APawn* TargetPawn = Cast<APawn>(TargetActor);
const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();
if (bTargetLocallyControlled)
{
const FVector ClientFinalLocation = TargetActor->GetActorLocation();
const float Distance = FVector::Dist(ServerFinalLocation, ClientFinalLocation);

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_MULTICAST_FINAL_SKIP_LOCAL_TARGET Time=%.3f Ctx=%d Target=%s Tag=%s DistToServer=%.2f ClientLoc=%s ServerLoc=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Distance,
*PP_VecToString(ClientFinalLocation),
*PP_VecToString(ServerFinalLocation));
return;
}

	if (!ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag))
	{
		return;
	}

	const FVector ClientFinalLocation = TargetActor->GetActorLocation();
	const FVector Delta = ServerFinalLocation - ClientFinalLocation;
	const float Distance = Delta.Size();
	const FRotator ClientFinalRotation = TargetActor->GetActorRotation();
	const float RotationDistance = FMath::Max3(
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Pitch - ClientFinalRotation.Pitch)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Yaw - ClientFinalRotation.Yaw)),
		FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Roll - ClientFinalRotation.Roll)));


	if (Distance <= FinalCorrectionTolerance && RotationDistance <= FinalRotationCorrectionTolerance) return;

	if (!bApplyInstantFinalCorrection) return;

	if (Distance > MaxInstantFinalCorrectionDistance) return;

	if (Distance > FinalCorrectionTolerance)
	{
		TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (RotationDistance > FinalRotationCorrectionTolerance)
	{
		TargetActor->SetActorRotation(ServerFinalRotation, ETeleportType::TeleportPhysics);
	}
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
		PendingPredictedReactions.RemoveAt(0, NumToRemove, EAllowShrinking::No);
	}
}

void UPP_PredictionComponent::RemoveExpiredPendingPredictedReactions()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
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

void UPP_PredictionComponent::ApplyTargetEffects(AActor* InstigatorActor, AActor* TargetActor,
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

void UPP_PredictionComponent::ApplyReactionTransform(AActor* InstigatorActor, AActor* TargetActor,
	const FPP_ReactionTransformSettings& TransformSettings) const
{
	if (!InstigatorActor || !TargetActor) return;

	const FPP_ReactionRotationSettings& RotationSettings = TransformSettings.RotationSettings;
	if (RotationSettings.RotationDirection != EPP_ReactionRotationDirection::None)
	{
		AActor* ReferenceActor = ResolveReactionReferenceActor(InstigatorActor, TargetActor,
			RotationSettings.ReferenceActorSource);

		ApplyReactionTransformToRecipients(InstigatorActor, TargetActor, ReferenceActor,
			RotationSettings.Recipient,
			[this, &RotationSettings](AActor* RecipientActor, AActor* InReferenceActor)
			{
				ApplyReactionRotation(RecipientActor, InReferenceActor, RotationSettings);
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
				ApplyReactionMovement(RecipientActor, InReferenceActor, MovementSettings);
			});
	}
}

void UPP_PredictionComponent::ApplyReactionTransformToRecipients(AActor* InstigatorActor, AActor* TargetActor,
	AActor* ReferenceActor, EPP_ReactionTransformRecipient Recipient,
	TFunctionRef<void(AActor* RecipientActor, AActor* ReferenceActor)> ApplyFunction) const
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
	const FPP_ReactionMovementSettings& MovementSettings) const
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

	RecipientActor->SetActorLocation(NewLocation, MovementSettings.bSweep, nullptr,
		ToTeleportType(MovementSettings.TeleportType));
}

void UPP_PredictionComponent::ApplyReactionRotation(AActor* RecipientActor, AActor* ReferenceActor,
	const FPP_ReactionRotationSettings& RotationSettings) const
{
	if (!RecipientActor || !ReferenceActor) return;

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();
	FRotator DesiredRotation = RecipientActor->GetActorRotation();

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
			FVector OppositeDirection = -ReferenceActor->GetActorForwardVector();
			OppositeDirection.Z = 0.f;
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
