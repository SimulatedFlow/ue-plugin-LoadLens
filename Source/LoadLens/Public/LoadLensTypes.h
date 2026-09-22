// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LoadLensTypes.generated.h"

/**
 * What LoadLens thinks of the blocking loads it has seen since the warm-up ended.
 *
 * The three values are also the three exit codes of LoadLens.Gate - 0, 1, 2 - and they mean the same
 * three things there as in LocaleGuard, AssetWarden and WidgetLedger, so a project that owns more than
 * one of these plugins does not have to learn a second convention.
 */
UENUM(BlueprintType)
enum class ELoadLensVerdict : uint8
{
	/** No more blocking loads after the warm-up than the budget allows. With the default budget: none. */
	Ok		UMETA(DisplayName = "Ok"),

	/** Over budget, but still at or under the warning limit. Something got through; not yet a wall. */
	Warn	UMETA(DisplayName = "Warn"),

	/** Above the warning limit. */
	Over	UMETA(DisplayName = "Over"),
};

/**
 * How the end of a blocking load was observed - and therefore how much the millisecond figure is worth.
 *
 * This enum exists because the engine hands out the start of a synchronous load on every platform and in
 * every build configuration, and the end of one only in some. Rather than quietly print a number that
 * means something different depending on how the project was compiled, every record says which of the
 * three it is, and the counter box marks the difference.
 */
UENUM(BlueprintType)
enum class ELoadLensTiming : uint8
{
	/**
	 * Exact. The end of the load was observed on FCoreUObjectDelegates::OnEndLoadPackage.
	 *
	 * Available in editor builds and in -game runs of an editor build. Not compiled into a runtime game
	 * without WITH_EDITOR, which is why the other two values exist.
	 */
	Exact		UMETA(DisplayName = "Exact"),

	/**
	 * Measured. The stall was followed while it happened, through the async loader's flush-update callback.
	 *
	 * That callback is fired from inside the blocking flush, on the game thread, in every build including
	 * Shipping. The figure is the time from the start of the load to the last progress signal seen inside
	 * it, so it can fall a fraction of a millisecond short of the true end - it never overshoots.
	 */
	Measured	UMETA(DisplayName = "Measured"),

	/**
	 * Upper bound. Nothing was seen between the start of the load and the next time LoadLens got the game
	 * thread back, so the figure is start-of-load to that moment: the load plus whatever else the frame
	 * did afterwards. Shown with a leading '<' so nobody quotes it as a measurement.
	 */
	UpperBound	UMETA(DisplayName = "Upper bound"),
};

/**
 * One package that was loaded synchronously, and everything LoadLens knows about it.
 *
 * There is exactly one of these per package name, not one per incident. A path that gets loaded a
 * hundred times is one line with Count 100 and the sum of the stalls - a log that writes a line per
 * incident makes the hitch it is measuring worse, which is the fastest way for a profiling tool to
 * become the problem.
 */
USTRUCT(BlueprintType)
struct LOADLENS_API FLoadLensRecord
{
	GENERATED_BODY()

	/** Long package name, e.g. "/Game/Weapons/BP_Rifle". Never a file path. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	FString PackageName;

	/** How many times this package was loaded synchronously. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 Count = 0;

	/** Of those, how many happened after the warm-up window closed. This is the number the budget judges. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 CountAfterWarmUp = 0;

	/** Every stall on this package added together, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float TotalMilliseconds = 0.0f;

	/** The single worst stall on this package, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float WorstMilliseconds = 0.0f;

	/** Frame number of the first time this package blocked. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int64 FirstFrame = 0;

	/** Frame number of the last time it did. Equal to FirstFrame when it only happened once. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int64 LastFrame = 0;

	/** Seconds since the current map finished loading, at the first incident. Negative before any map. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float FirstTimeSeconds = 0.0f;

	/** Seconds since the current map finished loading, at the last incident. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float LastTimeSeconds = 0.0f;

	/**
	 * Who asked for it, or "unknown caller".
	 *
	 * A best effort, and honest about being one. When the load came out of a Blueprint the name of the
	 * function and the class it ran on are in here; when it came out of another package's serialisation
	 * the outer package is named; when neither can be established this stays "unknown caller" and is never
	 * filled with a plausible-looking guess. A wrong culprit costs more time than no culprit.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	FString Caller;

	/** True while every incident on this package fell inside the warm-up window. Equivalent to CountAfterWarmUp == 0. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	bool bDuringWarmUp = true;

	/**
	 * True when this package was only ever pulled in underneath another synchronous load, as an import.
	 *
	 * Nested loads carry no milliseconds of their own: their time is already inside the outer load's
	 * figure, and adding it again would inflate the one number people quote. They are still listed,
	 * because "what did that one load actually drag in" is usually the interesting question.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	bool bNestedOnly = true;

	/** How the worst measurement on this package was observed. See ELoadLensTiming. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	ELoadLensTiming Timing = ELoadLensTiming::UpperBound;
};

/**
 * The bill: everything the records add up to, plus the verdict.
 *
 * Rebuilt whenever a record changes, so the counter box, the Blueprint library, the console dump and the
 * JSON report all read the same numbers.
 */
USTRUCT(BlueprintType)
struct LOADLENS_API FLoadLensSummary
{
	GENERATED_BODY()

	/** Blocking loads seen in total, warm-up included. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 TotalCount = 0;

	/** Blocking loads after the warm-up window closed. The number the budget judges. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 CountAfterWarmUp = 0;

	/** Distinct packages behind those loads. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 UniquePackages = 0;

	/** Every stall added together, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float TotalMilliseconds = 0.0f;

	/** Of that, the part that happened after the warm-up window. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float TotalMillisecondsAfterWarmUp = 0.0f;

	/** The package behind the single worst stall, or empty when nothing has blocked yet. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	FString WorstPackage;

	/** That stall, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float WorstMilliseconds = 0.0f;

	/** How that worst figure was observed. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	ELoadLensTiming WorstTiming = ELoadLensTiming::UpperBound;

	/** The budget this was judged against: blocking loads allowed after the warm-up. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 Budget = 0;

	/** Above this count the verdict is Over rather than Warn. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 WarnLimit = 0;

	/** Ok, Warn or Over. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	ELoadLensVerdict Verdict = ELoadLensVerdict::Ok;

	/** True once the warm-up window has closed and blocking loads start counting against the budget. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	bool bWarmUpOver = false;

	/** Seconds since the last map finished loading. Negative before any map has. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	float SecondsSinceMapLoad = -1.0f;

	/** Loads dropped by the ignore list. Reported, so an ignore list can never quietly hide its own effect. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 IgnoredCount = 0;

	/**
	 * Loads seen after the record table hit its cap and stopped taking new package names.
	 *
	 * Still counted, no longer named. Zero in every normal run; a big number here means the project is
	 * doing something that deserves a look on its own.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	int32 UntrackedCount = 0;

	/** True when at least one figure in the table is an upper bound rather than a measurement. */
	UPROPERTY(BlueprintReadOnly, Category = "LoadLens")
	bool bAnyUpperBound = false;
};
