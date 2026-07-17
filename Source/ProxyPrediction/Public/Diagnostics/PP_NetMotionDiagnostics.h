#pragma once

#include "CoreMinimal.h"

class AActor;

DECLARE_LOG_CATEGORY_EXTERN(LogPPNetMotion, Log, All);

/** Enables correlated ability, reaction, root-motion, and correction diagnostics. */
PROXYPREDICTION_API bool PP_IsNetMotionDiagnosticEnabled();

/** Compact actor/network identity used by the net-motion diagnostic log. */
PROXYPREDICTION_API FString PP_GetNetMotionActorContext(const AActor* Actor);
