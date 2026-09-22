// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LoadLensTypes.h"
#include "LoadLensStatics.generated.h"

class ULoadLensSubsystem;

/**
 * LoadLens's arithmetic, and the way into it from Blueprint.
 *
 * Everything in the first half of this class is static and needs no world, no engine and no loading at
 * all. Ranking the table, judging a count against a budget, folding a second incident on the same path
 * into one row and shortening a path for the counter box are the four places this plugin can be quietly
 * wrong, and none of them needs a running game to be tested. That is the whole reason they live here
 * instead of inside the recorder.
 */
UCLASS()
class LOADLENS_API ULoadLensStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --------------------------------------------------------------------------------------------------
	// Pure logic
	// --------------------------------------------------------------------------------------------------

	/**
	 * Rank packages worst first: most total milliseconds, then most loads after the warm-up, then most
	 * loads, then by name.
	 *
	 * The name is the last tiebreak and it is there so the ranking is total. Two packages level on every
	 * count must not be able to swap places between one frame and the next - a five-row list that
	 * reorders itself while somebody is reading it looks like the numbers are moving when they are not.
	 */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static TArray<FLoadLensRecord> RankRecords(const TArray<FLoadLensRecord>& Records);

	/**
	 * Judge a count of blocking loads against the budget.
	 *
	 * At or under the budget is Ok. Above it but at or under the warning limit is Warn. Above the warning
	 * limit is Over. With the defaults - budget 0, warning limit 5 - that is: 0 Ok, 1 Warn, 5 Warn,
	 * 6 Over.
	 *
	 * A warning limit below the budget is raised to the budget, which removes the warning band entirely:
	 * anything over budget is then Over. That is a legitimate setting, not a mistake, and it is what a
	 * project that wants the build to fail on the first blocking load asks for.
	 */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static ELoadLensVerdict EvaluateBudget(int32 BlockingLoadCount, int32 Budget = 0, int32 WarnLimit = 5);

	/** The exit code LoadLens.Gate returns for a verdict: 0 Ok, 1 Warn, 2 Over. */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static int32 VerdictToExitCode(ELoadLensVerdict Verdict);

	/** "Ok", "Warn" or "Over". Used by the log line, the report and the counter box. */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static FString VerdictToString(ELoadLensVerdict Verdict);

	/** "exact", "measured" or "upper bound". What the millisecond figure next to it is worth. */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static FString TimingToString(ELoadLensTiming Timing);

	/**
	 * The single biggest finding, in a sentence a person can read out loud.
	 *
	 * "/Game/Weapons/BP_Rifle blocked 3x for 96.3 ms, worst 41.8 ms - BP_Rifle_C::EquipWeapon" is worth
	 * more than the table above it, because it names the thing to go and fix. An empty table gets a clean
	 * sentence, never an empty string: a blank line at the bottom of the counter box looks like a bug in
	 * the tool, and somebody will report it as one.
	 */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static FString SummarizeWorst(const TArray<FLoadLensRecord>& Records);

	/**
	 * Shorten a long package path for the counter box without losing the name.
	 *
	 * The middle goes, the asset name never does - it is the part you paste into the content browser's
	 * search field. A path whose asset name alone is longer than MaxLength therefore comes back longer
	 * than MaxLength, on purpose: a truncated name is worse than a wide line.
	 */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static FString ShortenPath(const FString& PackageName, int32 MaxLength = 46);

	/**
	 * Whether a package is covered by one of the ignore prefixes.
	 *
	 * Prefix match against the long package name, case-insensitive, so "/Engine/" covers everything under
	 * it. Empty prefixes are skipped rather than treated as "matches everything" - a stray blank row in a
	 * settings array must not silently switch the whole plugin off.
	 */
	UFUNCTION(BlueprintPure, Category = "LoadLens|Logic")
	static bool IsPathIgnored(const FString& PackageName, const TArray<FString>& IgnoredPathPrefixes);

	/**
	 * Fold one incident into the table: a new row, or a second hit on a row that is already there.
	 *
	 * This is the counting rule the whole plugin rests on. The same path loaded twice is one row with
	 * Count 2 and the two stalls added together, never two rows and never two log lines.
	 *
	 * Not a UFUNCTION: it takes ten arguments and would be a miserable Blueprint node. It is public and
	 * static because it is the piece of the recorder worth testing without a running game.
	 *
	 * @param MaxTrackedPackages  Cap on distinct rows. At the cap a new package is refused and the
	 *                            function returns false, so the caller can count it as untracked.
	 * @return                    True when the incident landed in the table.
	 */
	static bool AccumulateRecord(
		TArray<FLoadLensRecord>& Records,
		TMap<FString, int32>& Lookup,
		const FString& PackageName,
		float Milliseconds,
		int64 FrameNumber,
		float TimeSeconds,
		const FString& Caller,
		bool bAfterWarmUp,
		bool bNested,
		ELoadLensTiming Timing,
		int32 MaxTrackedPackages);

	/** Add up a table into the totals and the verdict. Ignored and untracked counts are passed through. */
	static FLoadLensSummary BuildSummary(
		const TArray<FLoadLensRecord>& Records,
		int32 Budget,
		int32 WarnLimit,
		int32 IgnoredCount,
		int32 UntrackedCount);

	// --------------------------------------------------------------------------------------------------
	// Blueprint access to the live ledger
	// --------------------------------------------------------------------------------------------------

	/** The engine-wide LoadLens subsystem, or null before the engine is up. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	static ULoadLensSubsystem* GetLoadLens();

	/** Every package that has blocked, ranked worst first. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	static TArray<FLoadLensRecord> GetLoadLensRecords();

	/** The totals and the verdict. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	static FLoadLensSummary GetLoadLensSummary();

	/** Blocking loads after the warm-up window. The number the budget judges. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	static int32 GetBlockingLoadCount();

	/** True when more got through than the budget allows. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	static bool IsOverBudget();

	/** Throw the ledger away and start counting again. */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	static void ResetLoadLens();

	/** Show or hide the counter box. Same as LoadLens.Show / LoadLens.Hide. */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	static void SetLoadLensCounterBoxVisible(bool bVisible);
};
