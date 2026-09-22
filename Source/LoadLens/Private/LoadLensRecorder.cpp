// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LoadLensRecorder.h"

#include "CoreGlobals.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "LoadLensLog.h"
#include "LoadLensSettings.h"
#include "LoadLensStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/Script.h"
#include "UObject/Stack.h"
#include "UObject/UObjectThreadContext.h"

namespace LoadLens
{
	/** What goes in a record when the caller could not be established. Never a plausible-looking guess. */
	static const TCHAR* UnknownCaller = TEXT("unknown caller");
}

FLoadLensRecorder& FLoadLensRecorder::Get()
{
	// A function-local static, not a global: the module can be loaded at PreEarlyLoadingScreen, and the
	// order in which globals in different translation units are constructed is not something to bet a
	// start-up hook on.
	static FLoadLensRecorder Recorder;
	return Recorder;
}

// --------------------------------------------------------------------------------------------------
// Installing
// --------------------------------------------------------------------------------------------------

void FLoadLensRecorder::Install()
{
	if (bInstalled)
	{
		return;
	}

	// The hook. FCoreDelegates::OnSyncLoadPackage is broadcast from LoadPackageInternal, on the game
	// thread, before the flush that does the blocking - so this fires at the start of the stall and
	// carries the name of the package that caused it.
	SyncLoadHandle = FCoreDelegates::OnSyncLoadPackage.AddRaw(this, &FLoadLensRecorder::HandleSyncLoadPackage);

	// The second half of the measurement. The async loader broadcasts this from inside its flush loop,
	// on the game thread, in every build configuration including Shipping. While a blocking load is in
	// progress it is the only sign of life LoadLens gets, and it is what turns a start time into a
	// duration on platforms where the engine offers no end-of-load callback at all.
	FlushUpdateHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &FLoadLensRecorder::HandleFlushUpdate);

	// The backstop, and the gate's clock.
	EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FLoadLensRecorder::HandleEndFrame);

	// The warm-up clock starts when a map is up, not when the process starts. Everything before the first
	// map is start-up by definition, and start-up loads synchronously on purpose.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FLoadLensRecorder::HandlePostLoadMap);

#if LOADLENS_HAS_END_LOAD_HOOK
	EndLoadPackageHandle = FCoreUObjectDelegates::OnEndLoadPackage.AddRaw(this, &FLoadLensRecorder::HandleEndLoadPackage);
#endif

	bInstalled = true;
}

void FLoadLensRecorder::Uninstall()
{
	if (!bInstalled)
	{
		return;
	}

	FCoreDelegates::OnSyncLoadPackage.Remove(SyncLoadHandle);
	FCoreDelegates::OnAsyncLoadingFlushUpdate.Remove(FlushUpdateHandle);
	FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

#if LOADLENS_HAS_END_LOAD_HOOK
	FCoreUObjectDelegates::OnEndLoadPackage.Remove(EndLoadPackageHandle);
#endif

	SyncLoadHandle.Reset();
	FlushUpdateHandle.Reset();
	EndFrameHandle.Reset();
	PostLoadMapHandle.Reset();
	EndLoadPackageHandle.Reset();

	bScopeOpen = false;
	bInstalled = false;
}

void FLoadLensRecorder::ApplySettings()
{
	const ULoadLensSettings& Settings = ULoadLensSettings::Get();

	WarmUpSeconds = FMath::Max(Settings.WarmUpSeconds, 0.0f);
	Budget = FMath::Max(Settings.BlockingLoadBudget, 0);
	WarnLimit = Settings.WarnLimit;
	TopPathLines = FMath::Clamp(Settings.TopPathLines, 0, 24);
	MaxPathLength = FMath::Clamp(Settings.MaxPathLength, 12, 200);
	MaxTrackedPackages = FMath::Max(Settings.MaxTrackedPackages, 16);
	bShowCounterBox = Settings.bShowCounterBox;
	bLogEachBlockingLoad = Settings.bLogEachBlockingLoad;
	CounterBoxPosition = Settings.CounterBoxPosition;
	ReportPath = Settings.ReportPath;
	ProvokeAssetPath = Settings.ProvokeAssetPath;
	IgnoredPathPrefixes = Settings.IgnoredPathPrefixes;

	bSettingsApplied = true;

	// Anything caught before this moment was judged against the defaults. Re-judge it now rather than
	// leave a verdict on screen that was reached with a budget the project never asked for.
	RebuildSummary();

	UE_LOG(LogLoadLens, Log,
		TEXT("LoadLens: budget %d blocking load(s) after a warm-up of %.1f s, warning limit %d."),
		Budget, WarmUpSeconds, FMath::Max(WarnLimit, Budget));
}

// --------------------------------------------------------------------------------------------------
// The hook handlers
// --------------------------------------------------------------------------------------------------

void FLoadLensRecorder::HandleSyncLoadPackage(const FString& PackageName)
{
	// The engine only broadcasts this from the game thread, and every reading below depends on that -
	// the nesting counter and the Blueprint stack are both thread-local. Check rather than assume.
	if (!IsInGameThread())
	{
		return;
	}

	// The nesting counter is incremented immediately after this broadcast, so a zero here means this is
	// the outermost blocking load and anything above zero means the engine is already inside one - this
	// package is an import being dragged in by the load that is already running. That distinction is what
	// keeps the milliseconds from being counted twice.
	const int32 Depth = FUObjectThreadContext::Get().SyncLoadUsingAsyncLoaderCount;

	if (Depth > 0)
	{
		NoteNestedLoad(PackageName);
		return;
	}

	// A second outermost load means the previous one has finished, whether or not anybody told us. Close
	// it here: this is the earliest moment LoadLens can possibly know, and the earlier a measurement is
	// closed the tighter it is.
	if (bScopeOpen)
	{
		CloseOpenScope(FPlatformTime::Seconds(), /*bExact*/ false);
	}

	OpenScopeFor(PackageName);
}

void FLoadLensRecorder::HandleFlushUpdate()
{
	if (!bScopeOpen || !IsInGameThread())
	{
		return;
	}

	// Inside a synchronous load the nesting counter is above zero. Above zero means this callback is
	// coming out of the flush we are timing, so the clock is still running and this is how far it got.
	// At zero the flush is over and this is an ordinary async-loading tick in some later frame - which
	// means the load we are holding open ended some time before now.
	if (FUObjectThreadContext::Get().SyncLoadUsingAsyncLoaderCount > 0)
	{
		OpenScope.LastProgressSeconds = FPlatformTime::Seconds();
		OpenScope.bProgressSeen = true;
	}
	else
	{
		CloseOpenScope(FPlatformTime::Seconds(), /*bExact*/ false);
	}
}

#if LOADLENS_HAS_END_LOAD_HOOK
void FLoadLensRecorder::HandleEndLoadPackage(const FEndLoadPackageContext& Context)
{
	if (!bScopeOpen || !IsInGameThread())
	{
		return;
	}

	for (const UPackage* Package : Context.LoadedPackages)
	{
		if (Package && Package->GetFName().ToString() == OpenScope.PackageName)
		{
			CloseOpenScope(FPlatformTime::Seconds(), /*bExact*/ true);
			return;
		}
	}
}
#endif

void FLoadLensRecorder::HandleEndFrame()
{
	if (bScopeOpen)
	{
		// Nothing closed it earlier, so all that can honestly be said is "no more than this". The record
		// is marked accordingly and the counter box prints it with a leading '<'.
		CloseOpenScope(FPlatformTime::Seconds(), /*bExact*/ false);
	}

	if (bGateRunning)
	{
		GateSecondsRemaining -= FApp::GetDeltaTime();
		if (GateSecondsRemaining <= 0.0f)
		{
			FinishGate();
		}
	}
}

void FLoadLensRecorder::HandlePostLoadMap(UWorld* World)
{
	MapLoadedSeconds = FPlatformTime::Seconds();

	// Not a reset. A map change is the single most productive place to look for blocking loads, and
	// throwing the table away every time one happens would hide exactly that.
	RebuildSummary();
}

// --------------------------------------------------------------------------------------------------
// The measurement
// --------------------------------------------------------------------------------------------------

void FLoadLensRecorder::OpenScopeFor(const FString& PackageName)
{
	OpenScope = FOpenScope();
	OpenScope.PackageName = PackageName;
	OpenScope.StartSeconds = FPlatformTime::Seconds();
	OpenScope.Frame = static_cast<int64>(GFrameCounter);
	OpenScope.TimeSeconds = SecondsSinceMapLoad();
	OpenScope.bAfterWarmUp = IsAfterWarmUp();
	OpenScope.bIgnored = ULoadLensStatics::IsPathIgnored(PackageName, IgnoredPathPrefixes);

	// Asking who called costs a walk of the Blueprint stack, and it is asked here rather than when the
	// scope closes for the obvious reason: by then the stack has moved on.
	OpenScope.Caller = OpenScope.bIgnored ? FString(LoadLens::UnknownCaller) : GuessCaller();

	bScopeOpen = true;
}

void FLoadLensRecorder::CloseOpenScope(double EndSeconds, bool bExact)
{
	if (!bScopeOpen)
	{
		return;
	}

	bScopeOpen = false;

	ELoadLensTiming Timing = ELoadLensTiming::UpperBound;
	double MeasuredEnd = EndSeconds;

	if (bExact)
	{
		Timing = ELoadLensTiming::Exact;
	}
	else if (OpenScope.bProgressSeen)
	{
		// The last sign of life from inside the flush. It can fall a fraction of a millisecond short of
		// the true end; it cannot overshoot, which is the error worth having in a number people quote.
		Timing = ELoadLensTiming::Measured;
		MeasuredEnd = OpenScope.LastProgressSeconds;
	}

	const float Milliseconds = static_cast<float>(FMath::Max(MeasuredEnd - OpenScope.StartSeconds, 0.0) * 1000.0);

	if (OpenScope.bIgnored)
	{
		++Summary.IgnoredCount;
		RebuildSummary();
		return;
	}

	Commit(OpenScope.PackageName, Milliseconds, OpenScope.Frame, OpenScope.TimeSeconds,
		OpenScope.Caller, OpenScope.bAfterWarmUp, /*bNested*/ false, Timing);
}

void FLoadLensRecorder::NoteNestedLoad(const FString& PackageName)
{
	if (ULoadLensStatics::IsPathIgnored(PackageName, IgnoredPathPrefixes))
	{
		++Summary.IgnoredCount;
		return;
	}

	// No milliseconds. This package's time is already inside the outer load's figure, and adding it a
	// second time would inflate the total - the one number people quote at each other in a review.
	const FString Caller = bScopeOpen
		? FString::Printf(TEXT("import of %s"), *OpenScope.PackageName)
		: FString(LoadLens::UnknownCaller);

	Commit(PackageName, 0.0f, static_cast<int64>(GFrameCounter), SecondsSinceMapLoad(),
		Caller, IsAfterWarmUp(), /*bNested*/ true, ELoadLensTiming::Exact);
}

void FLoadLensRecorder::Commit(const FString& PackageName, float Milliseconds, int64 Frame, float TimeSeconds,
	const FString& Caller, bool bAfterWarmUp, bool bNested, ELoadLensTiming Timing)
{
	const bool bTracked = ULoadLensStatics::AccumulateRecord(Records, RecordLookup, PackageName, Milliseconds,
		Frame, TimeSeconds, Caller, bAfterWarmUp, bNested, Timing, MaxTrackedPackages);

	if (!bTracked)
	{
		++Summary.UntrackedCount;
	}

	RebuildSummary();

	if (bLogEachBlockingLoad)
	{
		// This is off by default, and the reason belongs right here: a log that writes a line for every
		// incident makes the hitch worse than the one it is measuring. Counting is the default; spamming
		// is a thing you switch on deliberately, for one afternoon, while chasing one specific load.
		UE_LOG(LogLoadLens, Warning, TEXT("LoadLens: blocking load %s (%.1f ms, %s)%s - %s"),
			*PackageName, Milliseconds, *ULoadLensStatics::TimingToString(Timing),
			bAfterWarmUp ? TEXT("") : TEXT(" [warm-up]"), *Caller);
	}

	if (const int32* Index = RecordLookup.Find(PackageName))
	{
		OnBlockingLoadMeasured.Broadcast(Records[*Index], Milliseconds);
	}
}

void FLoadLensRecorder::RebuildSummary()
{
	const int32 IgnoredCount = Summary.IgnoredCount;
	const int32 UntrackedCount = Summary.UntrackedCount;

	Summary = ULoadLensStatics::BuildSummary(Records, Budget, WarnLimit, IgnoredCount, UntrackedCount);
	Summary.bWarmUpOver = IsAfterWarmUp();
	Summary.SecondsSinceMapLoad = SecondsSinceMapLoad();
}

FString FLoadLensRecorder::GuessCaller()
{
#if DO_BLUEPRINT_GUARD
	// The best answer there is, when there is one: the Blueprint function that is running right now. A
	// LoadSynchronous in a weapon-switch graph shows up here as BP_Rifle_C::EquipWeapon, which is the
	// difference between "something blocked" and "go and look at this node".
	if (const FBlueprintContextTracker* Tracker = FBlueprintContextTracker::TryGet())
	{
		const TArrayView<const FFrame* const> Stack = Tracker->GetCurrentScriptStack();
		if (Stack.Num() > 0)
		{
			if (const FFrame* Top = Stack.Last())
			{
				const FString FunctionName = Top->Node ? Top->Node->GetName() : FString(TEXT("<unnamed>"));
				const UObject* Context = Top->Object;
				const FString ClassName = (Context && Context->GetClass()) ? Context->GetClass()->GetName() : FString();

				return ClassName.IsEmpty()
					? FunctionName
					: FString::Printf(TEXT("%s::%s"), *ClassName, *FunctionName);
			}
		}
	}
#endif

	// Second best: the load came out of another object's serialisation, so the object being serialised is
	// the thing that referenced it. Not the call site, but a real place to start looking.
	if (const FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext())
	{
		if (const UObject* Serialized = SerializeContext->SerializedObject)
		{
			return FString::Printf(TEXT("serialising %s"), *Serialized->GetPathName());
		}
	}

	// And when neither is available: nothing. Native code that calls LoadObject leaves no trace a plugin
	// can read without a stack walk, and a stack walk in a Shipping build gives addresses, not names. So
	// this stays "unknown caller" - see the documentation section that explains why.
	return LoadLens::UnknownCaller;
}

float FLoadLensRecorder::SecondsSinceMapLoad() const
{
	if (MapLoadedSeconds < 0.0)
	{
		return -1.0f;
	}

	return static_cast<float>(FPlatformTime::Seconds() - MapLoadedSeconds);
}

bool FLoadLensRecorder::IsAfterWarmUp() const
{
	if (MapLoadedSeconds < 0.0)
	{
		// No map has finished loading yet, so this is start-up, and start-up loads synchronously on
		// purpose. Nothing here counts against the budget.
		return false;
	}

	return (FPlatformTime::Seconds() - MapLoadedSeconds) >= static_cast<double>(WarmUpSeconds);
}

// --------------------------------------------------------------------------------------------------
// Knobs
// --------------------------------------------------------------------------------------------------

void FLoadLensRecorder::Reset()
{
	Records.Reset();
	RecordLookup.Reset();
	Summary = FLoadLensSummary();
	bScopeOpen = false;

	RebuildSummary();
}

void FLoadLensRecorder::SetBudget(int32 InBudget)
{
	Budget = FMath::Max(InBudget, 0);
	RebuildSummary();
}

void FLoadLensRecorder::SetWarnLimit(int32 InWarnLimit)
{
	WarnLimit = InWarnLimit;
	RebuildSummary();
}

// --------------------------------------------------------------------------------------------------
// Report and gate
// --------------------------------------------------------------------------------------------------

void FLoadLensRecorder::DumpToLog() const
{
	const TArray<FLoadLensRecord> Ranked = ULoadLensStatics::RankRecords(Records);

	UE_LOG(LogLoadLens, Display, TEXT("--- LoadLens: %d blocking load(s), %d after the warm-up, %.1f ms in total ---"),
		Summary.TotalCount, Summary.CountAfterWarmUp, Summary.TotalMilliseconds);

	for (const FLoadLensRecord& Record : Ranked)
	{
		UE_LOG(LogLoadLens, Display, TEXT("  %-64s x%-4d (%d after warm-up)  total %8.1f ms  worst %8.1f ms [%s]  frames %lld-%lld  %s%s"),
			*Record.PackageName, Record.Count, Record.CountAfterWarmUp,
			Record.TotalMilliseconds, Record.WorstMilliseconds,
			*ULoadLensStatics::TimingToString(Record.Timing),
			Record.FirstFrame, Record.LastFrame,
			*Record.Caller,
			Record.bNestedOnly ? TEXT("  [import]") : TEXT(""));
	}

	if (Summary.IgnoredCount > 0)
	{
		UE_LOG(LogLoadLens, Display, TEXT("  %d load(s) dropped by the ignore list - not counted."), Summary.IgnoredCount);
	}

	if (Summary.UntrackedCount > 0)
	{
		UE_LOG(LogLoadLens, Display, TEXT("  %d load(s) counted but not named: the table is at its cap of %d packages."),
			Summary.UntrackedCount, MaxTrackedPackages);
	}

	UE_LOG(LogLoadLens, Display, TEXT("  Verdict %s against a budget of %d (warning limit %d). %s"),
		*ULoadLensStatics::VerdictToString(Summary.Verdict), Summary.Budget, Summary.WarnLimit,
		*ULoadLensStatics::SummarizeWorst(Records));
}

bool FLoadLensRecorder::WriteReport(FString Path) const
{
	if (Path.IsEmpty())
	{
		Path = ReportPath.IsEmpty() ? FString(TEXT("Saved/LoadLens/report.json")) : ReportPath;
	}

	const FString AbsolutePath = FPaths::IsRelative(Path)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Path)
		: Path;

	const TArray<FLoadLensRecord> Ranked = ULoadLensStatics::RankRecords(Records);

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("plugin"), TEXT("LoadLens"));
	Writer->WriteValue(TEXT("reportVersion"), 1);
	Writer->WriteValue(TEXT("verdict"), ULoadLensStatics::VerdictToString(Summary.Verdict));
	Writer->WriteValue(TEXT("exitCode"), ULoadLensStatics::VerdictToExitCode(Summary.Verdict));
	Writer->WriteValue(TEXT("budget"), Summary.Budget);
	Writer->WriteValue(TEXT("warnLimit"), Summary.WarnLimit);
	Writer->WriteValue(TEXT("warmUpSeconds"), WarmUpSeconds);
	Writer->WriteValue(TEXT("warmUpOver"), Summary.bWarmUpOver);
	Writer->WriteValue(TEXT("blockingLoads"), Summary.TotalCount);
	Writer->WriteValue(TEXT("blockingLoadsAfterWarmUp"), Summary.CountAfterWarmUp);
	Writer->WriteValue(TEXT("uniquePackages"), Summary.UniquePackages);
	Writer->WriteValue(TEXT("totalMilliseconds"), Summary.TotalMilliseconds);
	Writer->WriteValue(TEXT("totalMillisecondsAfterWarmUp"), Summary.TotalMillisecondsAfterWarmUp);
	Writer->WriteValue(TEXT("worstPackage"), Summary.WorstPackage);
	Writer->WriteValue(TEXT("worstMilliseconds"), Summary.WorstMilliseconds);
	Writer->WriteValue(TEXT("worstTiming"), ULoadLensStatics::TimingToString(Summary.WorstTiming));
	Writer->WriteValue(TEXT("ignoredLoads"), Summary.IgnoredCount);
	Writer->WriteValue(TEXT("untrackedLoads"), Summary.UntrackedCount);
	Writer->WriteValue(TEXT("anyUpperBound"), Summary.bAnyUpperBound);
	Writer->WriteValue(TEXT("worstFinding"), ULoadLensStatics::SummarizeWorst(Records));

	Writer->WriteArrayStart(TEXT("packages"));
	for (const FLoadLensRecord& Record : Ranked)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("package"), Record.PackageName);
		Writer->WriteValue(TEXT("count"), Record.Count);
		Writer->WriteValue(TEXT("countAfterWarmUp"), Record.CountAfterWarmUp);
		Writer->WriteValue(TEXT("totalMilliseconds"), Record.TotalMilliseconds);
		Writer->WriteValue(TEXT("worstMilliseconds"), Record.WorstMilliseconds);
		Writer->WriteValue(TEXT("timing"), ULoadLensStatics::TimingToString(Record.Timing));
		Writer->WriteValue(TEXT("firstFrame"), Record.FirstFrame);
		Writer->WriteValue(TEXT("lastFrame"), Record.LastFrame);
		Writer->WriteValue(TEXT("firstTimeSeconds"), Record.FirstTimeSeconds);
		Writer->WriteValue(TEXT("lastTimeSeconds"), Record.LastTimeSeconds);
		Writer->WriteValue(TEXT("caller"), Record.Caller);
		Writer->WriteValue(TEXT("duringWarmUp"), Record.bDuringWarmUp);
		Writer->WriteValue(TEXT("importOnly"), Record.bNestedOnly);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();

	if (!FFileHelper::SaveStringToFile(Json, *AbsolutePath))
	{
		UE_LOG(LogLoadLens, Error, TEXT("LoadLens: could not write the report to '%s'."), *AbsolutePath);
		return false;
	}

	UE_LOG(LogLoadLens, Display, TEXT("LoadLens: report written to '%s'."), *AbsolutePath);
	return true;
}

void FLoadLensRecorder::BeginGate(float Seconds, bool bExitWhenDone)
{
	if (bGateRunning)
	{
		UE_LOG(LogLoadLens, Warning, TEXT("LoadLens: a gate run is already in progress."));
		return;
	}

	// A gate measures its own window. Whatever the map load did before the build server said "go" is not
	// what it asked about, and leaving it in the table would fail builds for a hitch nobody saw.
	Reset();

	bGateRunning = true;
	bGateExitWhenDone = bExitWhenDone;
	GateSecondsRemaining = FMath::Max(Seconds, 0.0f);

	UE_LOG(LogLoadLens, Display,
		TEXT("LoadLens: gate running for %.1f s against a budget of %d blocking load(s) after a %.1f s warm-up."),
		GateSecondsRemaining, Budget, WarmUpSeconds);
}

void FLoadLensRecorder::FinishGate()
{
	bGateRunning = false;

	// Anything still open belongs in this run's numbers, not in the next one's.
	if (bScopeOpen)
	{
		CloseOpenScope(FPlatformTime::Seconds(), /*bExact*/ false);
	}

	RebuildSummary();

	const int32 ExitCode = ULoadLensStatics::VerdictToExitCode(Summary.Verdict);

	WriteReport(FString());

	UE_LOG(LogLoadLens, Display,
		TEXT("LOADLENS GATE RESULT=%s loads=%d after_warmup=%d budget=%d warn=%d total_ms=%.1f exit=%d"),
		*ULoadLensStatics::VerdictToString(Summary.Verdict),
		Summary.TotalCount, Summary.CountAfterWarmUp, Summary.Budget, Summary.WarnLimit,
		Summary.TotalMilliseconds, ExitCode);
	UE_LOG(LogLoadLens, Display, TEXT("LOADLENS GATE WORST=%s"), *ULoadLensStatics::SummarizeWorst(Records));

	if (bGateExitWhenDone)
	{
		FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(ExitCode));
	}
}
