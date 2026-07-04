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
#include "HAL/IConsoleManager.h"
#include "GameplayEffect.h"



namespace
{
static TAutoConsoleVariable<int32> CVarPPReactionDebug(
TEXT("pp.ReactionDebug"),
0,
TEXT("Enable lightweight reaction prediction correction logs. 0=off, 1=on."),
ECVF_Default);

static bool PP_ShouldLogReactionDebug()
{
return CVarPPReactionDebug.GetValueOnGameThread() != 0;
}

static FString PP_VecCompact(const FVector& Vec)
{
return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Vec.X, Vec.Y, Vec.Z);
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
	
	AddPendingPredictedReaction(Context, TargetActor, ReactionTag);
	
	const float StartPosition = GetReactionStartPosition(Reaction);

	ApplyReactionTransform(GetOwner(), TargetActor, TransformSettings);
	
	const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);
	
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

	ApplyReactionTransform(OwnerActor, TargetActor, TransformSettings);
	ApplyTargetEffects(OwnerActor, TargetActor, Reaction);

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

const APawn* TargetPawn = Cast<APawn>(TargetActor);
const bool bTargetLocallyControlled = TargetPawn && TargetPawn->IsLocallyControlled();

const bool bConsumedPendingPrediction = ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag);
if (bConsumedPendingPrediction)
{
AddDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_CONFIRM_CONSUMED_PENDING Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s TargetLocal=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
bTargetLocallyControlled ? 1 : 0);
}

return;
}

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_CONFIRM_NO_PENDING Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s TargetLocal=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
bTargetLocallyControlled ? 1 : 0);
}

if (bTargetLocallyControlled)
{
if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_CONFIRM_SKIP_LOCAL_TARGET Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString());
}
return;
}

FPP_ReactionDataEntry Reaction;
if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

const float StartPosition = GetReactionStartPosition(Reaction);

ApplyReactionTransform(OwnerActor, TargetActor, TransformSettings);

const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_CONFIRM_PLAY_PROXY Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s Played=%d Loc=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
bPlayed ? 1 : 0,
*PP_VecCompact(TargetActor->GetActorLocation()));
}
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

ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
if (!TargetCharacter) return;

UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();

bool bAppliedCorrectionSuppression = false;

if (MovementComponent)
{
if (OwnerReactionCorrectionSuppressionCount == 0)
{
bSavedOwnerIgnoreClientMovementErrorChecksAndCorrection =
MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection;
bSavedOwnerClientIgnoreMovementCorrections =
MovementComponent->bClientIgnoreMovementCorrections;
}

OwnerReactionCorrectionSuppressionCount++;
bAppliedCorrectionSuppression = true;

MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection = true;
MovementComponent->bClientIgnoreMovementCorrections = true;

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_SUPPRESS_BEGIN Time=%.3f Ctx=%d Target=%s Tag=%s Count=%d SavedErr=%d SavedCorr=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
OwnerReactionCorrectionSuppressionCount,
bSavedOwnerIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0,
bSavedOwnerClientIgnoreMovementCorrections ? 1 : 0);
}
}

const float StartPosition = GetReactionStartPosition(Reaction);

ApplyReactionTransform(InstigatorActor, TargetActor, TransformSettings);

const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

if (!bPlayed)
{
if (bAppliedCorrectionSuppression)
{
OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);

if (OwnerReactionCorrectionSuppressionCount == 0 && MovementComponent)
{
ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("OwnerPlayFailed"));

MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection =
bSavedOwnerIgnoreClientMovementErrorChecksAndCorrection;
MovementComponent->bClientIgnoreMovementCorrections =
bSavedOwnerClientIgnoreMovementCorrections;
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
TWeakObjectPtr<AActor> WeakTarget(TargetActor);
const FGameplayTag CapturedReactionTag = ReactionTag;
const FPP_ReactionPredictionContext CapturedContext = Context;
const bool bCapturedAppliedCorrectionSuppression = bAppliedCorrectionSuppression;

FTimerHandle TimerHandle;
World->GetTimerManager().SetTimer(
TimerHandle,
[WeakThis, WeakMovementComponent, WeakTarget, CapturedReactionTag, CapturedContext,
bCapturedAppliedCorrectionSuppression]()
{
UPP_PredictionComponent* StrongThis = WeakThis.Get();
AActor* StrongTargetActor = WeakTarget.Get();
UCharacterMovementComponent* StrongMovementComponent = WeakMovementComponent.Get();

if (!StrongThis) return;

if (bCapturedAppliedCorrectionSuppression)
{
StrongThis->OwnerReactionCorrectionSuppressionCount =
FMath::Max(0, StrongThis->OwnerReactionCorrectionSuppressionCount - 1);
}

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_SUPPRESS_END Time=%.3f Ctx=%d Target=%s Tag=%s Count=%d"),
StrongTargetActor && StrongTargetActor->GetWorld() ? StrongTargetActor->GetWorld()->GetTimeSeconds() : -1.f,
CapturedContext.PredictionId,
*GetNameSafe(StrongTargetActor),
*CapturedReactionTag.ToString(),
StrongThis->OwnerReactionCorrectionSuppressionCount);
}

if (StrongThis->OwnerReactionCorrectionSuppressionCount > 0)
{
return;
}

if (StrongTargetActor)
{
StrongThis->ApplyOwnerPendingFinalReactionCorrection(CapturedContext, StrongTargetActor,
CapturedReactionTag, TEXT("OwnerReactionEnd"));
}

if (StrongMovementComponent)
{
StrongMovementComponent->bIgnoreClientMovementErrorChecksAndCorrection =
StrongThis->bSavedOwnerIgnoreClientMovementErrorChecksAndCorrection;
StrongMovementComponent->bClientIgnoreMovementCorrections =
StrongThis->bSavedOwnerClientIgnoreMovementCorrections;

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_RESTORE_CORRECTION_FLAGS Time=%.3f Ctx=%d Target=%s Tag=%s RestoreErr=%d RestoreCorr=%d"),
StrongTargetActor && StrongTargetActor->GetWorld() ? StrongTargetActor->GetWorld()->GetTimeSeconds() : -1.f,
CapturedContext.PredictionId,
*GetNameSafe(StrongTargetActor),
*CapturedReactionTag.ToString(),
StrongThis->bSavedOwnerIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0,
StrongThis->bSavedOwnerClientIgnoreMovementCorrections ? 1 : 0);
}
}
},
RemainingDuration,
false);
}
else if (MovementComponent)
{
if (bAppliedCorrectionSuppression)
{
OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);
}

if (OwnerReactionCorrectionSuppressionCount == 0)
{
ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("NoWorld"));

MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection =
bSavedOwnerIgnoreClientMovementErrorChecksAndCorrection;
MovementComponent->bClientIgnoreMovementCorrections =
bSavedOwnerClientIgnoreMovementCorrections;
}
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
if (UPP_PredictionComponent* TargetPredictionComponent =
TargetActor->FindComponentByClass<UPP_PredictionComponent>())
{
TargetPredictionComponent->AddOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
ServerFinalLocation, ServerFinalRotation);

if (TargetPredictionComponent->OwnerReactionCorrectionSuppressionCount == 0)
{
TargetPredictionComponent->ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor,
ReactionTag, TEXT("OwnerFinalArrivedAfterReaction"));
}
}

if (PP_ShouldLogReactionDebug())
{
const FVector ClientLocation = TargetActor->GetActorLocation();
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_STORE_LOCAL_TARGET Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s Dist=%.2f SuppressionCount=%d Client=%s Server=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
FVector::Dist(ClientLocation, ServerFinalLocation),
TargetActor->FindComponentByClass<UPP_PredictionComponent>()
? TargetActor->FindComponentByClass<UPP_PredictionComponent>()->OwnerReactionCorrectionSuppressionCount
: -1,
*PP_VecCompact(ClientLocation),
*PP_VecCompact(ServerFinalLocation));
}

return;
}

const bool bHadDeferredCorrection =
ConsumeDeferredPredictedReactionCorrection(Context, TargetActor, ReactionTag);
const bool bHadPendingPrediction =
ConsumePendingPredictedReaction(Context, TargetActor, ReactionTag);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_RECV Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s HadDeferred=%d HadPending=%d Client=%s Server=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
bHadDeferredCorrection ? 1 : 0,
bHadPendingPrediction ? 1 : 0,
*PP_VecCompact(TargetActor->GetActorLocation()),
*PP_VecCompact(ServerFinalLocation));
}

if (!bHadDeferredCorrection && !bHadPendingPrediction)
{
if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_DROP_NO_MARKER Time=%.3f Ctx=%d Target=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString());
}
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

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_DELTA Time=%.3f Ctx=%d Target=%s Tag=%s Dist=%.2f RotDist=%.2f Client=%s Server=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Distance,
RotationDistance,
*PP_VecCompact(ClientFinalLocation),
*PP_VecCompact(ServerFinalLocation));
}

if (Distance <= FinalCorrectionTolerance && RotationDistance <= FinalRotationCorrectionTolerance)
{
if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_WITHIN_TOLERANCE Time=%.3f Ctx=%d Target=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString());
}
return;
}

if (!bApplyInstantFinalCorrection)
{
if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_BLOCKED_BY_SETTING Time=%.3f Ctx=%d Target=%s Tag=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString());
}
return;
}

if (Distance > FinalCorrectionTolerance)
{
TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_APPLY_LOCATION Time=%.3f Ctx=%d Target=%s Tag=%s Dist=%.2f NewLoc=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Distance,
*PP_VecCompact(TargetActor->GetActorLocation()));
}
}

if (RotationDistance > FinalRotationCorrectionTolerance)
{
TargetActor->SetActorRotation(ServerFinalRotation, ETeleportType::TeleportPhysics);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_FINAL_APPLY_ROTATION Time=%.3f Ctx=%d Target=%s Tag=%s RotDist=%.2f"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
RotationDistance);
}
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

void UPP_PredictionComponent::AddOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, FGameplayTag ReactionTag, const FVector& ServerFinalLocation,
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

bool UPP_PredictionComponent::ConsumeOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, FGameplayTag ReactionTag, FVector& OutServerFinalLocation,
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

bool UPP_PredictionComponent::ApplyOwnerPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, FGameplayTag ReactionTag, const TCHAR* Reason)
{
if (!TargetActor) return false;

FVector ServerFinalLocation = FVector::ZeroVector;
FRotator ServerFinalRotation = FRotator::ZeroRotator;

if (!ConsumeOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
ServerFinalLocation, ServerFinalRotation))
{
if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_FINAL_NO_STORED Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"));
}

return false;
}

const FVector ClientFinalLocation = TargetActor->GetActorLocation();
const FRotator ClientFinalRotation = TargetActor->GetActorRotation();
const float Distance = FVector::Dist(ClientFinalLocation, ServerFinalLocation);
const float RotationDistance = FMath::Max3(
FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Pitch - ClientFinalRotation.Pitch)),
FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Yaw - ClientFinalRotation.Yaw)),
FMath::Abs(FRotator::NormalizeAxis(ServerFinalRotation.Roll - ClientFinalRotation.Roll)));

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_FINAL_APPLY_CHECK Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s Dist=%.2f RotDist=%.2f Client=%s Server=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"),
Distance,
RotationDistance,
*PP_VecCompact(ClientFinalLocation),
*PP_VecCompact(ServerFinalLocation));
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
