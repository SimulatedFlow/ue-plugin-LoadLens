// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LoadLens.h"

#include "LoadLensLog.h"
#include "LoadLensRecorder.h"
#include "Misc/CoreDelegates.h"

DEFINE_LOG_CATEGORY(LogLoadLens);

#define LOCTEXT_NAMESPACE "FLoadLensModule"

void FLoadLensModule::StartupModule()
{
	// This runs at PreEarlyLoadingScreen, which is the whole reason the plugin declares that phase: the
	// hook has to be hanging before the game starts loading its first map, or the most interesting
	// blocking loads happen with nobody listening.
	FLoadLensRecorder::Get().Install();

	// The project settings cannot be read yet - there is no UObject system at this point in start-up - so
	// the recorder runs on its defaults until the engine is up and then adopts the real ones.
	PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
	{
		FLoadLensRecorder::Get().ApplySettings();
	});

	UE_LOG(LogLoadLens, Log, TEXT("LoadLens started: watching for blocking loads."));
}

void FLoadLensModule::ShutdownModule()
{
	// FCoreDelegates lives in Core and outlives this DLL. A lambda left on it after the module has gone
	// is a call into unmapped memory the next time the engine finishes starting up.
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	FLoadLensRecorder::Get().Uninstall();

	UE_LOG(LogLoadLens, Log, TEXT("LoadLens shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLoadLensModule, LoadLens)
