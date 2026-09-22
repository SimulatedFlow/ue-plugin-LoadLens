// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "LoadLensLog.h"
#include "LoadLensRecorder.h"
#include "LoadLensStatics.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/**
 * The console surface.
 *
 * All of it runs against the engine-wide recorder rather than against a world, because the recorder is
 * engine-wide: a blocking load between two maps belongs to neither of them. None of these commands is
 * editor-only - they behave the same in play-in-editor and in a packaged Shipping build, which is the
 * only place some of these loads ever show up.
 */
namespace LoadLensCommands
{
	static bool ParseBool(const TArray<FString>& Args, bool bDefault)
	{
		if (Args.Num() == 0)
		{
			return bDefault;
		}

		return Args[0].ToBool() || Args[0] == TEXT("1");
	}

	static FAutoConsoleCommand GShow(
		TEXT("LoadLens.Show"),
		TEXT("LoadLens.Show [0|1] - show the on-screen counter box."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			FLoadLensRecorder::Get().SetShowCounterBox(ParseBool(Args, true));
		}));

	static FAutoConsoleCommand GHide(
		TEXT("LoadLens.Hide"),
		TEXT("LoadLens.Hide - hide the on-screen counter box."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FLoadLensRecorder::Get().SetShowCounterBox(false);
		}));

	static FAutoConsoleCommand GDump(
		TEXT("LoadLens.Dump"),
		TEXT("LoadLens.Dump - write the whole ledger to the log, not just the five rows the box shows."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FLoadLensRecorder::Get().DumpToLog();
		}));

	static FAutoConsoleCommand GReset(
		TEXT("LoadLens.Reset"),
		TEXT("LoadLens.Reset - throw the ledger away and start counting again."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FLoadLensRecorder::Get().Reset();
			UE_LOG(LogLoadLens, Display, TEXT("LoadLens: ledger cleared."));
		}));

	static FAutoConsoleCommand GBudget(
		TEXT("LoadLens.Budget"),
		TEXT("LoadLens.Budget <n> [warn] - blocking loads allowed after the warm-up, and the warning limit."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			FLoadLensRecorder& Recorder = FLoadLensRecorder::Get();

			if (Args.Num() == 0)
			{
				UE_LOG(LogLoadLens, Display, TEXT("LoadLens.Budget <n> [warn]  (currently %d, warning limit %d)"),
					Recorder.GetBudget(), Recorder.GetWarnLimit());
				return;
			}

			Recorder.SetBudget(FCString::Atoi(*Args[0]));

			if (Args.Num() > 1)
			{
				Recorder.SetWarnLimit(FCString::Atoi(*Args[1]));
			}

			UE_LOG(LogLoadLens, Display, TEXT("LoadLens: budget %d, warning limit %d."),
				Recorder.GetBudget(), Recorder.GetWarnLimit());
		}));

	static FAutoConsoleCommand GReport(
		TEXT("LoadLens.Report"),
		TEXT("LoadLens.Report [path] - write the ledger as JSON. Default: the project settings path."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			FLoadLensRecorder::Get().WriteReport(Args.Num() > 0 ? Args[0] : FString());
		}));

	/**
	 * The gate.
	 *
	 * Measures for the given number of seconds, writes Saved/LoadLens/report.json and ends the process
	 * with 0 for none, 1 under the warning limit and 2 above it. Those are the same three numbers
	 * LocaleGuard, AssetWarden and WidgetLedger return, and they mean the same three things, so a project
	 * that owns more than one of these does not have to keep two conventions in its head.
	 *
	 * -noexit measures and reports without ending the process, which is what you want when you are typing
	 * this into the console rather than running it from a build script.
	 */
	static FAutoConsoleCommand GGate(
		TEXT("LoadLens.Gate"),
		TEXT("LoadLens.Gate <seconds> [-noexit] - measure, write the report, exit 0 none / 1 warn / 2 over."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			float Seconds = 30.0f;
			bool bExitWhenDone = true;

			for (const FString& Arg : Args)
			{
				if (Arg.Equals(TEXT("-noexit"), ESearchCase::IgnoreCase))
				{
					bExitWhenDone = false;
				}
				else if (Arg.IsNumeric())
				{
					Seconds = FCString::Atof(*Arg);
				}
			}

			FLoadLensRecorder::Get().BeginGate(Seconds, bExitWhenDone);
		}));

	/**
	 * The demonstration.
	 *
	 * Loads an asset synchronously on purpose, so a buyer can watch the counter box go red without first
	 * having to build a bug into their own game. The number it produces is a real measurement of a real
	 * blocking load - there is nothing staged about it, which is exactly why it is worth having.
	 *
	 * A package that is already in memory cannot block, and the command says so instead of reporting a
	 * zero that looks like a broken plugin.
	 */
	static FAutoConsoleCommand GProvoke(
		TEXT("LoadLens.Provoke"),
		TEXT("LoadLens.Provoke [asset path] - deliberately trigger one blocking load, to see the box react."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			const FLoadLensRecorder& Recorder = FLoadLensRecorder::Get();

			const FString AssetPath = Args.Num() > 0 ? Args[0] : Recorder.GetProvokeAssetPath();
			if (AssetPath.IsEmpty())
			{
				UE_LOG(LogLoadLens, Warning,
					TEXT("LoadLens.Provoke: no asset to load. Pass one, or set Provoke Asset Path in the project settings."));
				return;
			}

			const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
			if (FindPackage(nullptr, *PackageName))
			{
				UE_LOG(LogLoadLens, Display,
					TEXT("LoadLens.Provoke: '%s' is already in memory, so loading it cannot block. Point the command at something cold."),
					*AssetPath);
				return;
			}

			const double StartSeconds = FPlatformTime::Seconds();

			// The thing the plugin exists to catch, written out in full so nobody has to wonder what the
			// demonstration actually did: a synchronous load on the game thread.
			UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);

			const double Milliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

			if (!Loaded)
			{
				UE_LOG(LogLoadLens, Warning,
					TEXT("LoadLens.Provoke: could not load '%s' (%.1f ms spent trying). Check the path."),
					*AssetPath, Milliseconds);
				return;
			}

			UE_LOG(LogLoadLens, Display,
				TEXT("LoadLens.Provoke: loaded '%s' synchronously in %.1f ms. The counter box has it now."),
				*AssetPath, Milliseconds);
		}));
}
