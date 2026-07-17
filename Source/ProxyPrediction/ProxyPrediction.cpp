// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProxyPrediction.h"
#include "Diagnostics/PP_NetMotionDiagnostics.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogPPNetMotion);

namespace
{
	TAutoConsoleVariable<int32> CVarPPNetMotionDiagnostics(
		TEXT("pp.NetMotionDiagnostics"),
		0,
		TEXT("Logs correlated gameplay ability, reaction prediction, root-motion, and movement-correction events.\n")
		TEXT("0: disabled (default), 1: enabled"),
		ECVF_Default);

	const TCHAR* PP_GetRoleName(const ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority: return TEXT("Authority");
		case ROLE_AutonomousProxy: return TEXT("AutonomousProxy");
		case ROLE_SimulatedProxy: return TEXT("SimulatedProxy");
		case ROLE_None:
		default: return TEXT("None");
		}
	}

	const TCHAR* PP_GetNetModeName(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

bool PP_IsNetMotionDiagnosticEnabled()
{
	return CVarPPNetMotionDiagnostics.GetValueOnGameThread() != 0;
}

FString PP_GetNetMotionActorContext(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("Actor=None");
	}

	const UWorld* World = Actor->GetWorld();
	return FString::Printf(
		TEXT("Actor=%s Role=%s RemoteRole=%s NetMode=%s Local=%d T=%.3f Loc=%s Vel=%s"),
		*GetNameSafe(Actor),
		PP_GetRoleName(Actor->GetLocalRole()),
		PP_GetRoleName(Actor->GetRemoteRole()),
		World ? PP_GetNetModeName(World->GetNetMode()) : TEXT("NoWorld"),
		Actor->HasLocalNetOwner() ? 1 : 0,
		World ? World->GetTimeSeconds() : -1.0,
		*Actor->GetActorLocation().ToCompactString(),
		*Actor->GetVelocity().ToCompactString());
}

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, ProxyPrediction, "ProxyPrediction" );

