// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * The LoadLens module.
 *
 * Its loading phase is PreEarlyLoadingScreen, and that is the one decision in this plugin that could not
 * be made any other way: the hook has to be hanging before the game starts loading, or the most
 * interesting blocking loads - the ones that happen while the first map is coming up - are gone before
 * anything is listening.
 *
 * That has a consequence worth knowing about: at this point in start-up the UObject system is not up yet,
 * so no UDeveloperSettings CDO can be read. The recorder therefore starts on its built-in defaults and
 * adopts the project settings on OnPostEngineInit. Anything caught in between is still counted, still
 * timed and still listed - it is simply judged against the defaults until the settings arrive.
 */
class FLoadLensModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

private:
	/** Kept so the callback can be taken off a Core delegate that outlives this module's DLL. */
	FDelegateHandle PostEngineInitHandle;
};
