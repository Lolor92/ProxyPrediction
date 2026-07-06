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
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "GameplayEffect.h"
#include "Component/SyncAbilityMotionComponent.h"
#include "Movement/SyncAbilityMotionCharacterMovementComponent.h"



namespace
{
static TAutoConsoleVariable<int32> CVarPPReactionDebug(
TEXT("pp.ReactionDebug"),
0,
TEXT("Enable lightweight reaction prediction correction logs. 0=off, 1=on."),
ECVF_Default);

static TAutoConsoleVariable<int32> CVarPPReactionProxyVerbose(
TEXT("pp.ReactionProxyVerbose"),
0,
TEXT("Enable verbose simulated-proxy reaction correction logs. 0=off, 1=on."),
ECVF_Default);

static bool PP_ShouldLogReactionDebug()
{
return CVarPPReactionDebug.GetValueOnGameThread() != 0;
}

static bool PP_ShouldLogProxyVerbose()
{
return PP_ShouldLogReactionDebug() && CVarPPReactionProxyVerbose.GetValueOnGameThread() != 0;
}

static FString PP_VecCompact(const FVector& Vec)
{
return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Vec.X, Vec.Y, Vec.Z);
}

static void PP_PrepareCharacterForReactionRootMotion(AActor* Actor, const TCHAR* Role, int32 PredictionId,
FGameplayTag ReactionTag)
{
ACharacter* Character = Cast<ACharacter>(Actor);
if (!Character) return;

UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
if (!MovementComponent) return;

const FVector BeforeLocation = Character->GetActorLocation();
const FVector BeforeVelocity = MovementComponent->Velocity;
const FVector BeforeAcceleration = MovementComponent->GetCurrentAcceleration();
const FVector BeforePendingInput = Character->GetPendingMovementInputVector();
const uint8 BeforeMovementMode = static_cast<uint8>(MovementComponent->MovementMode);
const bool bHadAnimRootMotion = MovementComponent->HasAnimRootMotion();
const bool bHadRootMotionSources = MovementComponent->CurrentRootMotion.HasActiveRootMotionSources();

MovementComponent->StopMovementImmediately();
MovementComponent->ClearAccumulatedForces();
Character->ConsumeMovementInputVector();
MovementComponent->Velocity = FVector::ZeroVector;
MovementComponent->UpdateComponentVelocity();

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PREP_ROOT_MOTION Time=%.3f Role=%s Ctx=%d Actor=%s Tag=%s Loc=%s VelBefore=%s AccelBefore=%s PendingInputBefore=%s MoveModeBefore=%d HasAnimRM=%d HasRMSources=%d VelAfter=%s"),
Character->GetWorld() ? Character->GetWorld()->GetTimeSeconds() : -1.f,
Role ? Role : TEXT("None"),
PredictionId,
*GetNameSafe(Character),
*ReactionTag.ToString(),
*PP_VecCompact(BeforeLocation),
*PP_VecCompact(BeforeVelocity),
*PP_VecCompact(BeforeAcceleration),
*PP_VecCompact(BeforePendingInput),
BeforeMovementMode,
bHadAnimRootMotion ? 1 : 0,
bHadRootMotionSources ? 1 : 0,
*PP_VecCompact(MovementComponent->Velocity));
}
}

static void PP_SetOwnerMontageTrackCorrectionSuppressed(UCharacterMovementComponent* MovementComponent,
	bool bSuppressed, const FPP_ReactionPredictionContext& Context, AActor* TargetActor, FGameplayTag ReactionTag,
	const TCHAR* Reason)
{
	USyncAbilityMotionCharacterMovementComponent* SyncMoveComponent =
		Cast<USyncAbilityMotionCharacterMovementComponent>(MovementComponent);
	if (!SyncMoveComponent) return;

	SyncMoveComponent->SetIgnoreServerRootMotionMontageTrackCorrection(bSuppressed);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_TRACK_CORRECTION_%s Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s"),
			bSuppressed ? TEXT("SUPPRESS_BEGIN") : TEXT("SUPPRESS_END"),
			TargetActor && TargetActor->GetWorld() ? TargetActor->GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			Reason ? Reason : TEXT("None"));
	}
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

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		TransformSettings.RotationSettings.bUseReferenceFacingOverride = true;
		TransformSettings.RotationSettings.ReferenceFacingOverride =
			FRotator(0.f, OwnerPawn->GetControlRotation().Yaw, 0.f);
	}

	ApplyLatestOlderProxyPendingFinalReactionCorrection(Context, TargetActor, TEXT("PredictedPreflight"));

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

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_SERVER_START_PRE Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s OwnerLoc=%s OwnerRot=%s TargetLoc=%s TargetRot=%s StartPos=%.3f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(OwnerActor),
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			*PP_VecCompact(OwnerActor->GetActorLocation()),
			*OwnerActor->GetActorRotation().ToCompactString(),
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString(),
			StartPosition);
	}

	ApplyReactionTransform(OwnerActor, TargetActor, TransformSettings);

	const FVector ServerStartLocation = TargetActor->GetActorLocation();
	const FRotator ServerStartRotation = TargetActor->GetActorRotation();
	TransformSettings.bUseServerStartTransform = true;
	TransformSettings.ServerStartLocation = ServerStartLocation;
	TransformSettings.ServerStartRotation = ServerStartRotation;

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_SERVER_START_POST_TRANSFORM Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s OwnerLoc=%s OwnerRot=%s TargetLoc=%s TargetRot=%s StartPos=%.3f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(OwnerActor),
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			*PP_VecCompact(OwnerActor->GetActorLocation()),
			*OwnerActor->GetActorRotation().ToCompactString(),
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString(),
			StartPosition);
	}

	ApplyTargetEffects(OwnerActor, TargetActor, Reaction);

	const bool bServerPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_SERVER_START_PLAY Time=%.3f Ctx=%d Target=%s Tag=%s Played=%d TargetLoc=%s TargetRot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			bServerPlayed ? 1 : 0,
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
	
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
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_SERVER_SEND_OWNER_RPC Time=%.3f Ctx=%d Owner=%s Target=%s TargetComp=%s Tag=%s TargetOwner=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(OwnerActor),
			*GetNameSafe(TargetActor),
			*GetNameSafe(TargetPredictionComponent),
			*ReactionTag.ToString(),
			*GetNameSafe(TargetActor->GetOwner()));

		TargetPredictionComponent->ClientPlayOwnerConfirmedReaction(Context, TargetActor, OwnerActor, ReactionTag,
			ServerStartLocation, ServerStartRotation, TransformSettings);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_SERVER_NO_TARGET_COMPONENT Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(OwnerActor),
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString());
	}
	
	MulticastPlayConfirmedReaction(Context, TargetActor, ReactionTag,
		ServerStartLocation, ServerStartRotation, TransformSettings);
}

void UPP_PredictionComponent::MulticastPlayConfirmedReaction_Implementation(FPP_ReactionPredictionContext Context,
AActor* TargetActor, FGameplayTag ReactionTag, FVector ServerStartLocation, FRotator ServerStartRotation,
FPP_ReactionTransformSettings TransformSettings)
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

if (PP_ShouldLogProxyVerbose())
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

if (PP_ShouldLogProxyVerbose())
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
if (PP_ShouldLogProxyVerbose())
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

ApplyLatestOlderProxyPendingFinalReactionCorrection(Context, TargetActor, TEXT("ConfirmedPreflight"));

TargetActor->SetActorLocationAndRotation(ServerStartLocation, ServerStartRotation,
	false, nullptr, ETeleportType::TeleportPhysics);

const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

if (PP_ShouldLogProxyVerbose())
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
	FVector ServerStartLocation, FRotator ServerStartRotation, FPP_ReactionTransformSettings TransformSettings)
{
AActor* OwnerActor = GetOwner();

UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_RPC_ENTRY Time=%.3f Ctx=%d ComponentOwner=%s Target=%s Instigator=%s Tag=%s OwnerLocal=%d TargetLocal=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor),
*GetNameSafe(InstigatorActor),
*ReactionTag.ToString(),
Cast<APawn>(OwnerActor) && Cast<APawn>(OwnerActor)->IsLocallyControlled() ? 1 : 0,
Cast<APawn>(TargetActor) && Cast<APawn>(TargetActor)->IsLocallyControlled() ? 1 : 0);

if (!OwnerActor || OwnerActor != TargetActor)
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_RPC_REJECT_OWNER_MISMATCH Time=%.3f Ctx=%d ComponentOwner=%s Target=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
*GetNameSafe(TargetActor));
return;
}

APawn* OwnerPawn = Cast<APawn>(OwnerActor);
if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_RPC_REJECT_NOT_LOCAL Time=%.3f Ctx=%d Owner=%s IsPawn=%d Local=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(OwnerActor),
OwnerPawn ? 1 : 0,
OwnerPawn && OwnerPawn->IsLocallyControlled() ? 1 : 0);
return;
}

if (!TargetActor || !ReactionData || !ReactionTag.IsValid())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_RPC_REJECT_BAD_DATA Time=%.3f Ctx=%d Target=%s ReactionData=%d TagValid=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
ReactionData ? 1 : 0,
ReactionTag.IsValid() ? 1 : 0);
return;
}

FPP_ReactionDataEntry Reaction;
if (!ReactionData->FindReaction(ReactionTag, Reaction)) return;

ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
if (!TargetCharacter) return;

UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();
USyncAbilityMotionCharacterMovementComponent* SyncMovementComponent =
	Cast<USyncAbilityMotionCharacterMovementComponent>(MovementComponent);
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
PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, true, Context, TargetActor, ReactionTag,
	TEXT("OwnerReactionStart"));

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_TRACK_SUPPRESS_BEGIN Time=%.3f Ctx=%d Target=%s Tag=%s Count=%d Smoothing=%d ErrChecks=%d ClientCorr=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
OwnerReactionCorrectionSuppressionCount,
static_cast<int32>(MovementComponent->NetworkSmoothingMode),
MovementComponent->bIgnoreClientMovementErrorChecksAndCorrection ? 1 : 0,
MovementComponent->bClientIgnoreMovementCorrections ? 1 : 0);
}
}

const float StartPosition = GetReactionStartPosition(Reaction);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_START_PRE Time=%.3f Ctx=%d Target=%s Instigator=%s Tag=%s TargetLoc=%s TargetRot=%s InstigatorLoc=%s InstigatorRot=%s StartPos=%.3f SuppressionCount=%d"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*GetNameSafe(InstigatorActor),
*ReactionTag.ToString(),
*PP_VecCompact(TargetActor->GetActorLocation()),
*TargetActor->GetActorRotation().ToCompactString(),
InstigatorActor ? *PP_VecCompact(InstigatorActor->GetActorLocation()) : TEXT("(None)"),
InstigatorActor ? *InstigatorActor->GetActorRotation().ToCompactString() : TEXT("None"),
StartPosition,
OwnerReactionCorrectionSuppressionCount);
}
	if (TransformSettings.bUseServerStartTransform)
	{
		TargetActor->SetActorLocationAndRotation(TransformSettings.ServerStartLocation,
			TransformSettings.ServerStartRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		TargetActor->SetActorLocationAndRotation(ServerStartLocation, ServerStartRotation,
			false, nullptr, ETeleportType::TeleportPhysics);
	}

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_START_POST_TRANSFORM Time=%.3f Ctx=%d Target=%s Instigator=%s Tag=%s TargetLoc=%s TargetRot=%s InstigatorLoc=%s InstigatorRot=%s StartPos=%.3f"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*GetNameSafe(InstigatorActor),
*ReactionTag.ToString(),
*PP_VecCompact(TargetActor->GetActorLocation()),
*TargetActor->GetActorRotation().ToCompactString(),
InstigatorActor ? *PP_VecCompact(InstigatorActor->GetActorLocation()) : TEXT("(None)"),
InstigatorActor ? *InstigatorActor->GetActorRotation().ToCompactString() : TEXT("None"),
StartPosition);
}

if (SyncMovementComponent)
{
	SyncMovementComponent->SetAbilityRootMotionSuppressed(true);
	bAppliedAbilityRootMotionSuppression = true;

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_ABILITY_RM_SUPPRESS_BEGIN Time=%.3f Ctx=%d Target=%s Tag=%s WasSuppressed=%d NowSuppressed=%d Loc=%s Rot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			bSavedAbilityRootMotionSuppressed ? 1 : 0,
			SyncMovementComponent->IsAbilityRootMotionSuppressed() ? 1 : 0,
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
}

if (OwnerAnimInstance)
{
	SavedOwnerRootMotionMode = OwnerAnimInstance->RootMotionMode.GetValue();
	OwnerAnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	bAppliedVisualOnlyRootMotion = true;

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_VISUAL_ONLY_BEGIN Time=%.3f Ctx=%d Target=%s Tag=%s SavedRootMotionMode=%d CurrentRootMotionMode=%d Loc=%s Rot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			static_cast<int32>(SavedOwnerRootMotionMode),
			static_cast<int32>(OwnerAnimInstance->RootMotionMode.GetValue()),
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
}

const bool bPlayed = PlayReactionMontageOnActor(TargetActor, Reaction, StartPosition, true);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_START_PLAY Time=%.3f Ctx=%d Target=%s Tag=%s Played=%d TargetLoc=%s TargetRot=%s StartPos=%.3f"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
bPlayed ? 1 : 0,
*PP_VecCompact(TargetActor->GetActorLocation()),
*TargetActor->GetActorRotation().ToCompactString(),
StartPosition);
}

if (!bPlayed)
{
if (bAppliedCorrectionSuppression)
{
OwnerReactionCorrectionSuppressionCount = FMath::Max(0, OwnerReactionCorrectionSuppressionCount - 1);

if (OwnerReactionCorrectionSuppressionCount == 0)
{
ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("OwnerPlayFailed"));

if (SyncMovementComponent && bAppliedAbilityRootMotionSuppression)
{
	SyncMovementComponent->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_ABILITY_RM_SUPPRESS_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RestoreSuppressed=%d Loc=%s Rot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			TEXT("OwnerPlayFailed"),
			bSavedAbilityRootMotionSuppressed ? 1 : 0,
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
}

if (OwnerAnimInstance && bAppliedVisualOnlyRootMotion)
{
	OwnerAnimInstance->SetRootMotionMode(SavedOwnerRootMotionMode);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_VISUAL_ONLY_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RestoreRootMotionMode=%d Loc=%s Rot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			TEXT("OwnerPlayFailed"),
			static_cast<int32>(SavedOwnerRootMotionMode),
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
}

if (MovementComponent)
{
	PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, false, Context, TargetActor, ReactionTag,
	TEXT("OwnerPlayFailed"));
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
TWeakObjectPtr<USyncAbilityMotionCharacterMovementComponent> WeakSyncMovementComponent(SyncMovementComponent);
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
USyncAbilityMotionCharacterMovementComponent* StrongSyncMovementComponent = WeakSyncMovementComponent.Get();
UAnimInstance* StrongOwnerAnimInstance = WeakOwnerAnimInstance.Get();

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

if (StrongSyncMovementComponent && bCapturedAppliedAbilityRootMotionSuppression)
{
	StrongSyncMovementComponent->SetAbilityRootMotionSuppressed(bCapturedSavedAbilityRootMotionSuppressed);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_ABILITY_RM_SUPPRESS_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RestoreSuppressed=%d Loc=%s Rot=%s"),
			StrongTargetActor && StrongTargetActor->GetWorld() ? StrongTargetActor->GetWorld()->GetTimeSeconds() : -1.f,
			CapturedContext.PredictionId,
			*GetNameSafe(StrongTargetActor),
			*CapturedReactionTag.ToString(),
			TEXT("OwnerReactionEnd"),
			bCapturedSavedAbilityRootMotionSuppressed ? 1 : 0,
			*PP_VecCompact(StrongTargetActor ? StrongTargetActor->GetActorLocation() : FVector::ZeroVector),
			StrongTargetActor ? *StrongTargetActor->GetActorRotation().ToCompactString() : TEXT("None"));
	}
}

if (StrongOwnerAnimInstance && bCapturedAppliedVisualOnlyRootMotion)
{
	StrongOwnerAnimInstance->SetRootMotionMode(CapturedSavedOwnerRootMotionMode);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_VISUAL_ONLY_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RestoreRootMotionMode=%d Loc=%s Rot=%s"),
			StrongTargetActor && StrongTargetActor->GetWorld() ? StrongTargetActor->GetWorld()->GetTimeSeconds() : -1.f,
			CapturedContext.PredictionId,
			*GetNameSafe(StrongTargetActor),
			*CapturedReactionTag.ToString(),
			TEXT("OwnerReactionEnd"),
			static_cast<int32>(CapturedSavedOwnerRootMotionMode),
			*PP_VecCompact(StrongTargetActor ? StrongTargetActor->GetActorLocation() : FVector::ZeroVector),
			StrongTargetActor ? *StrongTargetActor->GetActorRotation().ToCompactString() : TEXT("None"));
	}
}

if (StrongMovementComponent && bCapturedAppliedCorrectionSuppression)
{
PP_SetOwnerMontageTrackCorrectionSuppressed(StrongMovementComponent, false, CapturedContext, StrongTargetActor,
CapturedReactionTag, TEXT("OwnerReactionEnd"));

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_TRACK_SUPPRESS_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Smoothing=%d Loc=%s Rot=%s"),
StrongTargetActor && StrongTargetActor->GetWorld() ? StrongTargetActor->GetWorld()->GetTimeSeconds() : -1.f,
CapturedContext.PredictionId,
*GetNameSafe(StrongTargetActor),
*CapturedReactionTag.ToString(),
static_cast<int32>(StrongMovementComponent->NetworkSmoothingMode),
*PP_VecCompact(StrongTargetActor ? StrongTargetActor->GetActorLocation() : FVector::ZeroVector),
StrongTargetActor ? *StrongTargetActor->GetActorRotation().ToCompactString() : TEXT("None"));
}
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
ApplyOwnerPendingFinalReactionCorrection(Context, TargetActor, ReactionTag, TEXT("NoWorld"));

if (SyncMovementComponent && bAppliedAbilityRootMotionSuppression)
{
	SyncMovementComponent->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_ABILITY_RM_SUPPRESS_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RestoreSuppressed=%d Loc=%s Rot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			TEXT("NoWorld"),
			bSavedAbilityRootMotionSuppressed ? 1 : 0,
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
}

if (OwnerAnimInstance && bAppliedVisualOnlyRootMotion)
{
	OwnerAnimInstance->SetRootMotionMode(SavedOwnerRootMotionMode);

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_VISUAL_ONLY_RESTORE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RestoreRootMotionMode=%d Loc=%s Rot=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetActor),
			*ReactionTag.ToString(),
			TEXT("NoWorld"),
			static_cast<int32>(SavedOwnerRootMotionMode),
			*PP_VecCompact(TargetActor->GetActorLocation()),
			*TargetActor->GetActorRotation().ToCompactString());
	}
}

if (MovementComponent)
{
PP_SetOwnerMontageTrackCorrectionSuppressed(MovementComponent, false, Context, TargetActor, ReactionTag,
TEXT("NoWorld"));
}
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
TEXT("PP_REACTION_OWNER_FINAL_STORED Time=%.3f Ctx=%d Owner=%s Target=%s Tag=%s Dist=%.2f SuppressionCount=%d Client=%s Server=%s"),
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

if (PP_ShouldLogProxyVerbose())
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
if (PP_ShouldLogProxyVerbose())
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
TEXT("PP_REACTION_OWNER_FINAL_BEFORE_APPLY Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s Dist=%.2f RotDist=%.2f Client=%s Server=%s ClientRot=%s ServerRot=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"),
Distance,
RotationDistance,
*PP_VecCompact(ClientFinalLocation),
*PP_VecCompact(ServerFinalLocation),
*ClientFinalRotation.ToCompactString(),
*ServerFinalRotation.ToCompactString());
}

const bool bShouldApplyLocation = Distance > FinalCorrectionTolerance;
const bool bShouldApplyRotation = RotationDistance > FinalRotationCorrectionTolerance;
const float SmoothSeconds = FMath::Max(0.f, OwnerFinalCorrectionSmoothSeconds);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_FINAL_SMOOTH_START Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s ApplyLoc=%d ApplyRot=%d Dist=%.2f RotDist=%.2f SmoothSeconds=%.3f CurrentLoc=%s ServerLoc=%s CurrentRot=%s ServerRot=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"),
bShouldApplyLocation ? 1 : 0,
bShouldApplyRotation ? 1 : 0,
Distance,
RotationDistance,
SmoothSeconds,
*PP_VecCompact(ClientFinalLocation),
*PP_VecCompact(ServerFinalLocation),
*ClientFinalRotation.ToCompactString(),
*ServerFinalRotation.ToCompactString());
}

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

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_FINAL_SMOOTH_END Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s Instant=1 Loc=%s Rot=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"),
*PP_VecCompact(TargetActor->GetActorLocation()),
*TargetActor->GetActorRotation().ToCompactString());
}

return true;
}

TWeakObjectPtr<AActor> WeakTarget(TargetActor);
const FVector StartLocation = ClientFinalLocation;
const FQuat StartQuat = ClientFinalRotation.Quaternion();
const FVector TargetLocation = ServerFinalLocation;
const FQuat TargetQuat = ServerFinalRotation.Quaternion();
const float StartTime = World->GetTimeSeconds();
const FPP_ReactionPredictionContext CapturedContext = Context;
const FGameplayTag CapturedReactionTag = ReactionTag;
const FString CapturedReason = Reason ? FString(Reason) : FString(TEXT("None"));

TSharedRef<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();

World->GetTimerManager().SetTimer(
TimerHandle.Get(),
[WeakTarget, StartLocation, StartQuat, TargetLocation, TargetQuat, StartTime, SmoothSeconds,
bShouldApplyLocation, bShouldApplyRotation, CapturedContext, CapturedReactionTag, CapturedReason, TimerHandle]()
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

const FVector CurrentLocation = bShouldApplyLocation
? FMath::Lerp(StartLocation, TargetLocation, SmoothAlpha)
: StrongTarget->GetActorLocation();

const FRotator CurrentRotation = bShouldApplyRotation
? FQuat::Slerp(StartQuat, TargetQuat, SmoothAlpha).Rotator()
: StrongTarget->GetActorRotation();

StrongTarget->SetActorLocationAndRotation(CurrentLocation, CurrentRotation, false, nullptr, ETeleportType::TeleportPhysics);

if (RawAlpha >= 1.f)
{
StrongTarget->SetActorLocationAndRotation(
bShouldApplyLocation ? TargetLocation : StrongTarget->GetActorLocation(),
bShouldApplyRotation ? TargetQuat.Rotator() : StrongTarget->GetActorRotation(),
false,
nullptr,
ETeleportType::TeleportPhysics);

InnerWorld->GetTimerManager().ClearTimer(TimerHandle.Get());

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_OWNER_FINAL_SMOOTH_END Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s Instant=0 Loc=%s Rot=%s"),
InnerWorld->GetTimeSeconds(),
CapturedContext.PredictionId,
*GetNameSafe(StrongTarget),
*CapturedReactionTag.ToString(),
*CapturedReason,
*PP_VecCompact(StrongTarget->GetActorLocation()),
*StrongTarget->GetActorRotation().ToCompactString());
}
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


void UPP_PredictionComponent::AddProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, FGameplayTag ReactionTag, const FVector& ServerFinalLocation,
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

if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_BUFFERED Time=%.3f Ctx=%d Target=%s Tag=%s Updated=1 Loc=%s Rot=%s"),
World->GetTimeSeconds(),
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
*PP_VecCompact(ServerFinalLocation),
*ServerFinalRotation.ToCompactString());
}

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

if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_BUFFERED Time=%.3f Ctx=%d Target=%s Tag=%s Updated=0 Loc=%s Rot=%s"),
World->GetTimeSeconds(),
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
*PP_VecCompact(ServerFinalLocation),
*ServerFinalRotation.ToCompactString());
}
}

bool UPP_PredictionComponent::ConsumeProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, FGameplayTag ReactionTag, FVector& OutServerFinalLocation,
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

bool UPP_PredictionComponent::ApplyProxyPendingFinalReactionCorrection(const FPP_ReactionPredictionContext& Context,
AActor* TargetActor, FGameplayTag ReactionTag, const TCHAR* Reason)
{
if (!TargetActor) return false;

const bool bPreflightApply = Reason && FCString::Stristr(Reason, TEXT("Preflight")) != nullptr;
if (!bPreflightApply && HasNewerReactionMarkerForTarget(Context, TargetActor))
{
FVector DroppedServerFinalLocation = FVector::ZeroVector;
FRotator DroppedServerFinalRotation = FRotator::ZeroRotator;
ConsumeProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
DroppedServerFinalLocation, DroppedServerFinalRotation);

if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_DROP_NEWER_ACTIVE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"));
}

return false;
}

FVector ServerFinalLocation = FVector::ZeroVector;
FRotator ServerFinalRotation = FRotator::ZeroRotator;

if (!ConsumeProxyPendingFinalReactionCorrection(Context, TargetActor, ReactionTag,
ServerFinalLocation, ServerFinalRotation))
{
if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_NO_STORED Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s"),
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

if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_APPLY_CHECK Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s Dist=%.2f RotDist=%.2f Client=%s Server=%s"),
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

if (Distance <= FinalCorrectionTolerance && RotationDistance <= FinalRotationCorrectionTolerance)
{
if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_WITHIN_TOLERANCE Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"));
}

return true;
}

if (!bApplyInstantFinalCorrection)
{
if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_BLOCKED_BY_SETTING Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"));
}

return false;
}

if (Distance > FinalCorrectionTolerance)
{
TargetActor->SetActorLocation(ServerFinalLocation, false, nullptr, ETeleportType::TeleportPhysics);

if (PP_ShouldLogReactionDebug())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_FINAL_APPLY_LOCATION Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s Dist=%.2f NewLoc=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"),
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
TEXT("PP_REACTION_PROXY_FINAL_APPLY_ROTATION Time=%.3f Ctx=%d Target=%s Tag=%s Reason=%s RotDist=%.2f"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
*GetNameSafe(TargetActor),
*ReactionTag.ToString(),
Reason ? Reason : TEXT("None"),
RotationDistance);
}
}

return true;
}

bool UPP_PredictionComponent::ApplyLatestOlderProxyPendingFinalReactionCorrection(
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

if (PP_ShouldLogProxyVerbose())
{
UE_LOG(LogTemp, Warning,
TEXT("PP_REACTION_PROXY_PREFLIGHT_APPLY_OLDER_FINAL Time=%.3f NewCtx=%d OldCtx=%d Target=%s OldTag=%s Reason=%s"),
GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
Context.PredictionId,
Entry.PredictionId,
*GetNameSafe(TargetActor),
*Entry.ReactionTag.ToString(),
Reason ? Reason : TEXT("None"));
}

FPP_ReactionPredictionContext OlderContext;
OlderContext.PredictionId = Entry.PredictionId;

return ApplyProxyPendingFinalReactionCorrection(OlderContext, TargetActor, Entry.ReactionTag, Reason);
}

bool UPP_PredictionComponent::HasNewerReactionMarkerForTarget(
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

void UPP_PredictionComponent::PrepareOwnerReactionRootMotionState(ACharacter* TargetCharacter,
	const FPP_ReactionPredictionContext& Context, FGameplayTag ReactionTag) const
{
	if (!TargetCharacter) return;

	UCharacterMovementComponent* MovementComponent = TargetCharacter->GetCharacterMovement();
	USyncAbilityMotionCharacterMovementComponent* SyncMoveComponent =
		Cast<USyncAbilityMotionCharacterMovementComponent>(MovementComponent);
	USyncAbilityMotionComponent* SyncMotionComponent =
		TargetCharacter->FindComponentByClass<USyncAbilityMotionComponent>();
	USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;

	const bool bWasAbilityRootMotionSuppressed =
		SyncMoveComponent && SyncMoveComponent->IsAbilityRootMotionSuppressed();
	const bool bWasAbilityMovementInputSuppressed =
		SyncMoveComponent && SyncMoveComponent->IsAbilityMovementInputSuppressed();
	const float SavedRootMotionScale = TargetCharacter->GetAnimRootMotionTranslationScale();

	if (SyncMotionComponent)
	{
		SyncMotionComponent->ResetAbilityMotionState();
	}

	if (SyncMoveComponent)
	{
		SyncMoveComponent->SetAbilityRootMotionSuppressed(false);
		SyncMoveComponent->SetAbilityMovementInputSuppressed(false);
	}

	TargetCharacter->SetAnimRootMotionTranslationScale(1.0f);
	if (AnimInstance)
	{
		AnimInstance->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	}

	if (PP_ShouldLogReactionDebug())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PP_REACTION_OWNER_ROOT_MOTION_READY Time=%.3f Ctx=%d Target=%s Tag=%s HadSyncMotion=%d HadSyncMove=%d WasRMSuppressed=%d WasInputSuppressed=%d PrevScale=%.3f NewScale=%.3f HasAnim=%d Vel=%s Accel=%s HasAnimRM=%d HasRMSources=%d"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
			Context.PredictionId,
			*GetNameSafe(TargetCharacter),
			*ReactionTag.ToString(),
			SyncMotionComponent ? 1 : 0,
			SyncMoveComponent ? 1 : 0,
			bWasAbilityRootMotionSuppressed ? 1 : 0,
			bWasAbilityMovementInputSuppressed ? 1 : 0,
			SavedRootMotionScale,
			TargetCharacter->GetAnimRootMotionTranslationScale(),
			AnimInstance ? 1 : 0,
			MovementComponent ? *PP_VecCompact(MovementComponent->Velocity) : TEXT("(None)"),
			MovementComponent ? *PP_VecCompact(MovementComponent->GetCurrentAcceleration()) : TEXT("(None)"),
			MovementComponent && MovementComponent->HasAnimRootMotion() ? 1 : 0,
			MovementComponent && MovementComponent->CurrentRootMotion.HasActiveRootMotionSources() ? 1 : 0);
	}
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
