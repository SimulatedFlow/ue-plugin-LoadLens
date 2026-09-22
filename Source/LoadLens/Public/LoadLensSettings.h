// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LoadLensSettings.generated.h"

/**
 * Project-wide settings for LoadLens.
 *
 * Project Settings > Plugins > LoadLens, stored in DefaultGame.ini. Read on OnPostEngineInit and again
 * whenever a value changes in the editor. They are read that late for a reason: the hook is installed at
 * PreEarlyLoadingScreen, long before there is a UObject system to read a CDO from. Blocking loads caught
 * before the settings arrive are counted and timed against the defaults below.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "LoadLens"))
class LOADLENS_API ULoadLensSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULoadLensSettings();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;
	//~ End UDeveloperSettings interface

#if WITH_EDITOR
	//~ Begin UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	/** Convenience accessor. Never returns null. */
	static const ULoadLensSettings& Get();

	// --------------------------------------------------------------------------------------------------
	// Budget
	// --------------------------------------------------------------------------------------------------

	/**
	 * Seconds after a map finishes loading during which blocking loads do not count against the budget.
	 *
	 * A map load always brings some - the level's own assets, the game mode, the HUD - and a tool that
	 * goes red every time somebody presses Play is a tool people turn off in the first week. Three seconds
	 * is enough for a level to settle and short enough that a blocking load in the first thing the player
	 * does still gets caught.
	 *
	 * Zero means everything counts, from the first frame of the map.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0.0", UIMax = "30.0", Units = "s"))
	float WarmUpSeconds = 3.0f;

	/**
	 * How many blocking loads are allowed after the warm-up before the project is over budget.
	 *
	 * Zero, and that is not a placeholder. Every synchronous load during play is a frame somebody sees
	 * stop. The number exists so a project with one it has decided to live with can say so out loud
	 * instead of ignoring a red box.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "64"))
	int32 BlockingLoadBudget = 0;

	/**
	 * The count above which the verdict is Over instead of Warn - and the gate returns 2 instead of 1.
	 *
	 * With the defaults: none is Ok and exits 0, one to five is Warn and exits 1, six or more is Over and
	 * exits 2. Set it equal to the budget to remove the warning band entirely, so anything over budget
	 * fails the build outright.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "256"))
	int32 WarnLimit = 5;

	/**
	 * Package paths that do not count.
	 *
	 * Matched as a prefix against the long package name, case-insensitively, so "/Engine/" covers
	 * everything under it. The engine's own packages are here by default because a project cannot do
	 * anything about them and a list that opens with forty rows nobody can act on is a list nobody reads.
	 *
	 * Ignored loads are counted in the summary as IgnoredCount and shown in the counter box, so this list
	 * can never quietly hide its own effect.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget")
	TArray<FString> IgnoredPathPrefixes;

	// --------------------------------------------------------------------------------------------------
	// Counter box
	// --------------------------------------------------------------------------------------------------

	/** Draw the counter box. Same as LoadLens.Show / LoadLens.Hide. */
	UPROPERTY(Config, EditAnywhere, Category = "Counter Box")
	bool bShowCounterBox = true;

	/** How many packages the counter box lists under the header, worst first. */
	UPROPERTY(Config, EditAnywhere, Category = "Counter Box", meta = (ClampMin = "0", ClampMax = "24"))
	int32 TopPathLines = 5;

	/** Top left corner of the counter box, in pixels. */
	UPROPERTY(Config, EditAnywhere, Category = "Counter Box", meta = (ClampMin = "0.0"))
	FVector2D CounterBoxPosition = FVector2D(24.0f, 90.0f);

	/**
	 * How wide a package path may get in the counter box before it is shortened in the middle.
	 *
	 * The asset name is never shortened - it is the part you search for.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Counter Box", meta = (ClampMin = "12", ClampMax = "200"))
	int32 MaxPathLength = 46;

	// --------------------------------------------------------------------------------------------------
	// Report
	// --------------------------------------------------------------------------------------------------

	/**
	 * Where LoadLens.Gate and LoadLens.Report write. Relative paths are relative to the project directory.
	 *
	 * The same file both of them write by default, so a build server and a developer end up looking at the
	 * same document.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Report")
	FString ReportPath = TEXT("Saved/LoadLens/report.json");

	/**
	 * Write a log line for every blocking load, on top of counting it.
	 *
	 * Off by default, and the comment on that default belongs in the code: a log that writes a line per
	 * incident makes the hitch worse than the one it is measuring. Turn it on when you are chasing one
	 * specific load and want it in the log next to everything else that happened in that frame.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Report")
	bool bLogEachBlockingLoad = false;

	/**
	 * Upper limit on how many distinct package names are kept.
	 *
	 * Once it is reached, further blocking loads on new packages are still counted in the summary but no
	 * longer given a row of their own. A cap is the difference between a diagnostic tool and a memory
	 * leak; the summary reports how many loads it swallowed so the cap can never hide.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Report", meta = (ClampMin = "16", UIMax = "8192"))
	int32 MaxTrackedPackages = 512;

	// --------------------------------------------------------------------------------------------------
	// Provoke
	// --------------------------------------------------------------------------------------------------

	/**
	 * The asset LoadLens.Provoke loads synchronously, on purpose.
	 *
	 * It exists so a buyer can watch the box go red without first building a bug into their own game. The
	 * default points at the plugin's own deliberately heavy demo texture; point it at something of your
	 * own to see what one of your assets really costs to load cold.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Provoke")
	FString ProvokeAssetPath = TEXT("/LoadLens/LoadLens/Assets/T_LoadLensHeavy.T_LoadLensHeavy");
};
