// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "LoadLensTypes.h"
#include "UObject/UObjectGlobals.h"

/**
 * Whether the engine hands this build an exact end-of-load callback.
 *
 * FCoreUObjectDelegates::OnEndLoadPackage is compiled into editor builds and into builds that enable the
 * asset read logger, and into nothing else. The start of a synchronous load is available everywhere; the
 * end is not. LoadLens therefore has a second and a third way of closing a measurement, and every record
 * says which one it used - see ELoadLensTiming.
 */
#if WITH_EDITOR || (defined(UE_ENABLE_ASSET_READ_LOGGER) && UE_ENABLE_ASSET_READ_LOGGER)
	#define LOADLENS_HAS_END_LOAD_HOOK 1
#else
	#define LOADLENS_HAS_END_LOAD_HOOK 0
#endif

/**
 * The book-keeper. Plain C++, no UObject, and that is deliberate.
 *
 * The module loads at PreEarlyLoadingScreen so the hook is hanging before the game starts loading, and at
 * that point in start-up there is no UObject system to hold a subsystem. So the recorder is an ordinary
 * singleton, it installs itself from StartupModule, and ULoadLensSubsystem is a thin Blueprint-facing
 * shell around it that arrives later and finds the ledger already running.
 *
 * Everything in here runs on the game thread. The engine's sync-load hook is documented as game-thread
 * only, and every reading depends on that - so the handlers check it rather than assume it.
 */
class LOADLENS_API FLoadLensRecorder
{
public:
	/** The one recorder. Created on first use, which is FLoadLensModule::StartupModule. */
	static FLoadLensRecorder& Get();

	/** Hang the hooks. Called from StartupModule; safe to call twice. */
	void Install();

	/** Take them down again. Called from ShutdownModule. */
	void Uninstall();

	bool IsInstalled() const { return bInstalled; }

	/**
	 * Adopt the project settings.
	 *
	 * Called on OnPostEngineInit - the first moment a UDeveloperSettings CDO exists - and again whenever
	 * one of the values is edited. Everything caught before that was judged against the defaults in this
	 * header, and is re-judged the moment the real budget arrives.
	 */
	void ApplySettings();

	// --------------------------------------------------------------------------------------------------
	// The ledger
	// --------------------------------------------------------------------------------------------------

	/** One entry per package, unranked. Use ULoadLensStatics::RankRecords to order them. */
	const TArray<FLoadLensRecord>& GetRecords() const { return Records; }

	/** The totals and the verdict. Rebuilt whenever a record changes. */
	const FLoadLensSummary& GetSummary() const { return Summary; }

	/** Throw the ledger away and start counting again. The warm-up clock is not restarted. */
	void Reset();

	/** True when more blocking loads got through after the warm-up than the budget allows. */
	bool IsOverBudget() const { return Summary.Verdict != ELoadLensVerdict::Ok; }

	/** Write the ledger as JSON. An empty path means the one from the project settings. */
	bool WriteReport(FString Path) const;

	/** Write every row to the log - the whole table, not just the five the counter box shows. */
	void DumpToLog() const;

	// --------------------------------------------------------------------------------------------------
	// Knobs the console commands turn
	// --------------------------------------------------------------------------------------------------

	void SetBudget(int32 InBudget);
	int32 GetBudget() const { return Budget; }

	void SetWarnLimit(int32 InWarnLimit);
	int32 GetWarnLimit() const { return WarnLimit; }

	void SetShowCounterBox(bool bInShow) { bShowCounterBox = bInShow; }
	bool IsShowingCounterBox() const { return bShowCounterBox; }

	float GetWarmUpSeconds() const { return WarmUpSeconds; }
	int32 GetTopPathLines() const { return TopPathLines; }
	int32 GetMaxPathLength() const { return MaxPathLength; }
	FVector2D GetCounterBoxPosition() const { return CounterBoxPosition; }
	const FString& GetReportPath() const { return ReportPath; }
	const FString& GetProvokeAssetPath() const { return ProvokeAssetPath; }

	// --------------------------------------------------------------------------------------------------
	// The gate
	// --------------------------------------------------------------------------------------------------

	/**
	 * Measure for a while, write the report, and end the process with the verdict as its exit code.
	 *
	 * Clears the ledger first: a gate measures its own window, so what the build server sees is what
	 * happened during the run it asked for and not what happened while the map was still coming up.
	 */
	void BeginGate(float Seconds, bool bExitWhenDone);

	bool IsGateRunning() const { return bGateRunning; }

	// --------------------------------------------------------------------------------------------------
	// Signals
	// --------------------------------------------------------------------------------------------------

	/**
	 * Fired once a blocking load has been measured and folded into its record.
	 *
	 * Deliberately not fired when the load starts: at that moment the only thing known is the name, and a
	 * signal that carries a package and no cost is a signal nobody can act on.
	 */
	TMulticastDelegate<void(const FLoadLensRecord& /*Record*/, float /*Milliseconds*/)> OnBlockingLoadMeasured;

private:
	// --------------------------------------------------------------------------------------------------
	// Hook handlers
	// --------------------------------------------------------------------------------------------------

	/** FCoreDelegates::OnSyncLoadPackage. Fires at the start of the blocking load, before the flush. */
	void HandleSyncLoadPackage(const FString& PackageName);

	/** FCoreDelegates::OnAsyncLoadingFlushUpdate. Fires from inside the flush, so it follows the stall. */
	void HandleFlushUpdate();

	/** FCoreDelegates::OnEndFrame. The last resort for closing a measurement, and the gate's clock. */
	void HandleEndFrame();

	/** FCoreUObjectDelegates::PostLoadMapWithWorld. Restarts the warm-up clock. */
	void HandlePostLoadMap(class UWorld* World);

#if LOADLENS_HAS_END_LOAD_HOOK
	/** FCoreUObjectDelegates::OnEndLoadPackage. The exact end, where the engine offers one. */
	void HandleEndLoadPackage(const FEndLoadPackageContext& Context);
#endif

	// --------------------------------------------------------------------------------------------------
	// The measurement in progress
	// --------------------------------------------------------------------------------------------------

	/** One blocking load, between the moment it started and the moment LoadLens got the thread back. */
	struct FOpenScope
	{
		FString PackageName;
		FString Caller;
		double StartSeconds = 0.0;
		double LastProgressSeconds = 0.0;
		bool bProgressSeen = false;
		int64 Frame = 0;
		float TimeSeconds = -1.0f;
		bool bAfterWarmUp = false;
		bool bIgnored = false;
	};

	void OpenScopeFor(const FString& PackageName);
	void CloseOpenScope(double EndSeconds, bool bExact);
	void NoteNestedLoad(const FString& PackageName);

	/** Fold one incident into the table and rebuild the summary. */
	void Commit(const FString& PackageName, float Milliseconds, int64 Frame, float TimeSeconds,
		const FString& Caller, bool bAfterWarmUp, bool bNested, ELoadLensTiming Timing);

	void RebuildSummary();

	/**
	 * A best effort at naming who asked, and honest about being one.
	 *
	 * Returns "unknown caller" rather than something plausible when it cannot tell. See the documentation
	 * section "Why some loads show no caller" - that sentence exists because the alternative, a guessed
	 * name, sends somebody to the wrong file for an afternoon.
	 */
	static FString GuessCaller();

	/** Seconds since the current map finished loading, or -1 before any map has. */
	float SecondsSinceMapLoad() const;

	/** Whether a load happening right now counts against the budget. */
	bool IsAfterWarmUp() const;

	void FinishGate();

	// --------------------------------------------------------------------------------------------------
	// State
	// --------------------------------------------------------------------------------------------------

	TArray<FLoadLensRecord> Records;
	TMap<FString, int32> RecordLookup;
	FLoadLensSummary Summary;

	FOpenScope OpenScope;
	bool bScopeOpen = false;

	/** When the last map finished loading, in FPlatformTime seconds. Negative before the first one. */
	double MapLoadedSeconds = -1.0;

	bool bInstalled = false;
	bool bSettingsApplied = false;

	// Defaults, and they matter: everything caught between PreEarlyLoadingScreen and OnPostEngineInit is
	// judged against exactly these numbers. They are the same values ULoadLensSettings starts with.
	float WarmUpSeconds = 3.0f;
	int32 Budget = 0;
	int32 WarnLimit = 5;
	int32 TopPathLines = 5;
	int32 MaxPathLength = 46;
	int32 MaxTrackedPackages = 512;
	bool bShowCounterBox = true;
	bool bLogEachBlockingLoad = false;
	FVector2D CounterBoxPosition = FVector2D(24.0f, 90.0f);
	FString ReportPath = TEXT("Saved/LoadLens/report.json");
	FString ProvokeAssetPath;
	TArray<FString> IgnoredPathPrefixes;

	bool bGateRunning = false;
	bool bGateExitWhenDone = true;
	float GateSecondsRemaining = 0.0f;

	FDelegateHandle SyncLoadHandle;
	FDelegateHandle FlushUpdateHandle;
	FDelegateHandle EndFrameHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle EndLoadPackageHandle;
};
