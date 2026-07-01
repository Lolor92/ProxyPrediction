// Copyright Epic Games, Inc. All Rights Reserved.

#include "SyncInput.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "FSyncInputModule"

void FSyncInputModule::StartupModule()
{
}

void FSyncInputModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSyncInputModule, SyncInput)