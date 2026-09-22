// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Algo/Reverse.h"
#include "LoadLensStatics.h"
#include "LoadLensTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace LoadLensTests
{
	// CommandletContext as well as EditorContext: what is checked here is arithmetic a packaged build
	// runs, and a test that only exists when somebody has the editor open is a test that will not be
	// there on the build server when it matters.
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::EngineFilter;

	/** TestEqual has no overload for a scoped enum, and a failure message that reads "0 != 1" is fine here. */
	int32 AsInt(ELoadLensVerdict Verdict)
	{
		return static_cast<int32>(Verdict);
	}

	FLoadLensRecord MakeRecord(const TCHAR* Package, int32 Count, float TotalMs, float WorstMs)
	{
		FLoadLensRecord Record;
		Record.PackageName = Package;
		Record.Count = Count;
		Record.CountAfterWarmUp = Count;
		Record.TotalMilliseconds = TotalMs;
		Record.WorstMilliseconds = WorstMs;
		Record.Caller = TEXT("unknown caller");
		Record.bDuringWarmUp = false;
		Record.bNestedOnly = false;
		Record.Timing = ELoadLensTiming::Exact;
		return Record;
	}
}

//
// (1) RankRecords. Worst first by total time, and a total order so nothing can swap places on a tie.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLoadLensRankRecordsTest,
	"LoadLens.Logic.RankRecordsSortsByTotalTimeAndIsStableOnTies",
	LoadLensTests::TestFlags)

bool FLoadLensRankRecordsTest::RunTest(const FString& Parameters)
{
	TArray<FLoadLensRecord> Records;
	Records.Add(LoadLensTests::MakeRecord(TEXT("/Game/B"), 1, 10.0f, 10.0f));
	Records.Add(LoadLensTests::MakeRecord(TEXT("/Game/A"), 1, 96.3f, 41.8f));
	Records.Add(LoadLensTests::MakeRecord(TEXT("/Game/C"), 1, 50.0f, 50.0f));

	const TArray<FLoadLensRecord> Ranked = ULoadLensStatics::RankRecords(Records);

	TestEqual(TEXT("The most expensive package comes first"), Ranked[0].PackageName, FString(TEXT("/Game/A")));
	TestEqual(TEXT("Then the next one down"), Ranked[1].PackageName, FString(TEXT("/Game/C")));
	TestEqual(TEXT("Then the cheapest"), Ranked[2].PackageName, FString(TEXT("/Game/B")));

	// Three rows level on every count. The order must be decided by the name and must not depend on the
	// order they arrived in - a five-row list that reorders itself twice a second is unreadable.
	TArray<FLoadLensRecord> Tied;
	Tied.Add(LoadLensTests::MakeRecord(TEXT("/Game/Zulu"), 2, 20.0f, 10.0f));
	Tied.Add(LoadLensTests::MakeRecord(TEXT("/Game/Alpha"), 2, 20.0f, 10.0f));
	Tied.Add(LoadLensTests::MakeRecord(TEXT("/Game/Mike"), 2, 20.0f, 10.0f));

	const TArray<FLoadLensRecord> RankedTied = ULoadLensStatics::RankRecords(Tied);
	TestEqual(TEXT("Ties are broken by name, first"), RankedTied[0].PackageName, FString(TEXT("/Game/Alpha")));
	TestEqual(TEXT("Ties are broken by name, second"), RankedTied[1].PackageName, FString(TEXT("/Game/Mike")));
	TestEqual(TEXT("Ties are broken by name, third"), RankedTied[2].PackageName, FString(TEXT("/Game/Zulu")));

	Algo::Reverse(Tied);
	const TArray<FLoadLensRecord> RankedReversed = ULoadLensStatics::RankRecords(Tied);
	for (int32 Index = 0; Index < RankedTied.Num(); ++Index)
	{
		TestEqual(TEXT("The same rows in the other order rank identically"),
			RankedReversed[Index].PackageName, RankedTied[Index].PackageName);
	}

	// An empty table must come back empty rather than throw or invent a row.
	TestEqual(TEXT("An empty table ranks to an empty table"), ULoadLensStatics::RankRecords({}).Num(), 0);

	return true;
}

//
// (2) EvaluateBudget. The boundaries, exactly - off by one here means a build fails for nothing.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLoadLensEvaluateBudgetTest,
	"LoadLens.Logic.EvaluateBudgetHitsTheBoundariesExactly",
	LoadLensTests::TestFlags)

bool FLoadLensEvaluateBudgetTest::RunTest(const FString& Parameters)
{
	using LoadLensTests::AsInt;

	// The defaults: no blocking loads allowed, a warning band up to five.
	TestEqual(TEXT("None is Ok"), AsInt(ULoadLensStatics::EvaluateBudget(0, 0, 5)), AsInt(ELoadLensVerdict::Ok));
	TestEqual(TEXT("One is over budget, so Warn"), AsInt(ULoadLensStatics::EvaluateBudget(1, 0, 5)), AsInt(ELoadLensVerdict::Warn));
	TestEqual(TEXT("Sitting exactly on the warning limit is still Warn"), AsInt(ULoadLensStatics::EvaluateBudget(5, 0, 5)), AsInt(ELoadLensVerdict::Warn));
	TestEqual(TEXT("One above the warning limit is Over"), AsInt(ULoadLensStatics::EvaluateBudget(6, 0, 5)), AsInt(ELoadLensVerdict::Over));

	// A budget somebody has decided to live with. Sitting exactly on it is not a failure - it is the budget.
	TestEqual(TEXT("Exactly on a non-zero budget is Ok"), AsInt(ULoadLensStatics::EvaluateBudget(2, 2, 4)), AsInt(ELoadLensVerdict::Ok));
	TestEqual(TEXT("One over it is Warn"), AsInt(ULoadLensStatics::EvaluateBudget(3, 2, 4)), AsInt(ELoadLensVerdict::Warn));
	TestEqual(TEXT("Above the warning limit is Over"), AsInt(ULoadLensStatics::EvaluateBudget(5, 2, 4)), AsInt(ELoadLensVerdict::Over));

	// A warning limit at the budget removes the band entirely: over budget is Over, with no soft step.
	TestEqual(TEXT("No warning band: on budget is Ok"), AsInt(ULoadLensStatics::EvaluateBudget(2, 2, 2)), AsInt(ELoadLensVerdict::Ok));
	TestEqual(TEXT("No warning band: one over is Over"), AsInt(ULoadLensStatics::EvaluateBudget(3, 2, 2)), AsInt(ELoadLensVerdict::Over));

	// A warning limit below the budget describes a band that cannot exist; it is raised to the budget
	// rather than allowed to turn every reading into Over.
	TestEqual(TEXT("A nonsensical warning limit still says Ok on budget"), AsInt(ULoadLensStatics::EvaluateBudget(4, 4, 1)), AsInt(ELoadLensVerdict::Ok));
	TestEqual(TEXT("A nonsensical warning limit says Over above it"), AsInt(ULoadLensStatics::EvaluateBudget(5, 4, 1)), AsInt(ELoadLensVerdict::Over));

	// And the exit codes the build server reads.
	TestEqual(TEXT("Ok exits 0"), ULoadLensStatics::VerdictToExitCode(ELoadLensVerdict::Ok), 0);
	TestEqual(TEXT("Warn exits 1"), ULoadLensStatics::VerdictToExitCode(ELoadLensVerdict::Warn), 1);
	TestEqual(TEXT("Over exits 2"), ULoadLensStatics::VerdictToExitCode(ELoadLensVerdict::Over), 2);

	return true;
}

//
// (3) The ignore list. What it drops, and - just as important - what it must not.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLoadLensIgnoreListTest,
	"LoadLens.Logic.IgnoredPathsDoNotCount",
	LoadLensTests::TestFlags)

bool FLoadLensIgnoreListTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Ignored = { TEXT("/Engine/"), TEXT("/Script/") };

	TestTrue(TEXT("An engine package is ignored"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Engine/BasicShapes/Cube"), Ignored));
	TestTrue(TEXT("Matching is case-insensitive"),
		ULoadLensStatics::IsPathIgnored(TEXT("/engine/EngineMaterials/DefaultMaterial"), Ignored));
	TestTrue(TEXT("A script package is ignored"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Script/Engine"), Ignored));

	TestFalse(TEXT("A game package is not ignored"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Game/Weapons/BP_Rifle"), Ignored));
	TestFalse(TEXT("A prefix must match at the front, not in the middle"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Game/Engine/Thing"), Ignored));

	// A blank row in a settings array is a typo. It must not switch the whole plugin off.
	const TArray<FString> WithBlank = { TEXT(""), TEXT("/Engine/") };
	TestFalse(TEXT("An empty prefix does not match everything"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Game/Weapons/BP_Rifle"), WithBlank));
	TestTrue(TEXT("...and the real prefix next to it still works"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Engine/BasicShapes/Cube"), WithBlank));

	TestFalse(TEXT("An empty ignore list ignores nothing"),
		ULoadLensStatics::IsPathIgnored(TEXT("/Game/Weapons/BP_Rifle"), {}));

	return true;
}

//
// (4) The counting rule. The same path twice is one row, never two - and never two log lines.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLoadLensAccumulateTest,
	"LoadLens.Logic.SamePathTwiceIsOneRowWithCountTwoAndAddedTime",
	LoadLensTests::TestFlags)

bool FLoadLensAccumulateTest::RunTest(const FString& Parameters)
{
	TArray<FLoadLensRecord> Records;
	TMap<FString, int32> Lookup;

	const FString Path = TEXT("/Game/Weapons/BP_Rifle");

	ULoadLensStatics::AccumulateRecord(Records, Lookup, Path, 41.8f, 120, 4.0f,
		TEXT("BP_Rifle_C::EquipWeapon"), /*bAfterWarmUp*/ true, /*bNested*/ false, ELoadLensTiming::Exact, 512);
	ULoadLensStatics::AccumulateRecord(Records, Lookup, Path, 12.5f, 340, 9.5f,
		TEXT("BP_Rifle_C::EquipWeapon"), /*bAfterWarmUp*/ true, /*bNested*/ false, ELoadLensTiming::Exact, 512);

	TestEqual(TEXT("Two loads of one path are one row"), Records.Num(), 1);
	TestEqual(TEXT("Counted twice"), Records[0].Count, 2);
	TestEqual(TEXT("Both counted after the warm-up"), Records[0].CountAfterWarmUp, 2);
	TestEqual(TEXT("The times are added"), Records[0].TotalMilliseconds, 54.3f, 0.01f);
	TestEqual(TEXT("The worst is the worst, not the last"), Records[0].WorstMilliseconds, 41.8f, 0.01f);
	TestEqual(TEXT("The first frame is kept"), Records[0].FirstFrame, static_cast<int64>(120));
	TestEqual(TEXT("The last frame moves"), Records[0].LastFrame, static_cast<int64>(340));

	// A different path is a different row.
	ULoadLensStatics::AccumulateRecord(Records, Lookup, TEXT("/Game/Weapons/BP_Pistol"), 3.0f, 341, 9.6f,
		TEXT("unknown caller"), true, false, ELoadLensTiming::Measured, 512);
	TestEqual(TEXT("A different path gets its own row"), Records.Num(), 2);

	// A load during the warm-up counts on the row but not against the budget.
	TArray<FLoadLensRecord> WarmUpRecords;
	TMap<FString, int32> WarmUpLookup;
	ULoadLensStatics::AccumulateRecord(WarmUpRecords, WarmUpLookup, Path, 30.0f, 4, 0.5f,
		TEXT("unknown caller"), /*bAfterWarmUp*/ false, false, ELoadLensTiming::Exact, 512);
	TestEqual(TEXT("A warm-up load is still counted"), WarmUpRecords[0].Count, 1);
	TestEqual(TEXT("...but not against the budget"), WarmUpRecords[0].CountAfterWarmUp, 0);
	TestTrue(TEXT("...and the row says so"), WarmUpRecords[0].bDuringWarmUp);

	// An import that is later loaded in its own right stops being an import and takes the real caller.
	TArray<FLoadLensRecord> ImportRecords;
	TMap<FString, int32> ImportLookup;
	ULoadLensStatics::AccumulateRecord(ImportRecords, ImportLookup, Path, 0.0f, 10, 1.0f,
		TEXT("import of /Game/Maps/L_Demo"), true, /*bNested*/ true, ELoadLensTiming::Exact, 512);
	TestTrue(TEXT("An import-only row says so"), ImportRecords[0].bNestedOnly);
	TestEqual(TEXT("An import carries no time of its own"), ImportRecords[0].TotalMilliseconds, 0.0f, 0.001f);

	ULoadLensStatics::AccumulateRecord(ImportRecords, ImportLookup, Path, 20.0f, 200, 6.0f,
		TEXT("BP_Rifle_C::EquipWeapon"), true, /*bNested*/ false, ELoadLensTiming::Exact, 512);
	TestFalse(TEXT("A real load clears the import flag"), ImportRecords[0].bNestedOnly);
	TestEqual(TEXT("...and the real caller replaces the import note"),
		ImportRecords[0].Caller, FString(TEXT("BP_Rifle_C::EquipWeapon")));

	// The cap refuses new rows and says so, so the caller can count the load as untracked instead of
	// dropping it silently.
	TArray<FLoadLensRecord> Capped;
	TMap<FString, int32> CappedLookup;
	TestTrue(TEXT("The first row fits under a cap of one"),
		ULoadLensStatics::AccumulateRecord(Capped, CappedLookup, TEXT("/Game/One"), 1.0f, 1, 1.0f, TEXT(""), true, false, ELoadLensTiming::Exact, 1));
	TestFalse(TEXT("The second one is refused"),
		ULoadLensStatics::AccumulateRecord(Capped, CappedLookup, TEXT("/Game/Two"), 1.0f, 1, 1.0f, TEXT(""), true, false, ELoadLensTiming::Exact, 1));
	TestTrue(TEXT("...but a second hit on a row that exists still lands"),
		ULoadLensStatics::AccumulateRecord(Capped, CappedLookup, TEXT("/Game/One"), 1.0f, 2, 1.0f, TEXT(""), true, false, ELoadLensTiming::Exact, 1));
	TestEqual(TEXT("The cap held"), Capped.Num(), 1);
	TestEqual(TEXT("The surviving row counted both of its hits"), Capped[0].Count, 2);

	// And the summary built from that table.
	const FLoadLensSummary Summary = ULoadLensStatics::BuildSummary(Records, 0, 5, 7, 0);
	TestEqual(TEXT("The summary counts every load"), Summary.TotalCount, 3);
	TestEqual(TEXT("...and every package"), Summary.UniquePackages, 2);
	TestEqual(TEXT("...and names the worst one"), Summary.WorstPackage, Path);
	TestEqual(TEXT("...and passes the ignored count through, so the list cannot hide"), Summary.IgnoredCount, 7);
	TestEqual(TEXT("...and is over budget"), LoadLensTests::AsInt(Summary.Verdict), LoadLensTests::AsInt(ELoadLensVerdict::Warn));

	return true;
}

//
// (5) ShortenPath. The middle goes; the asset name never does.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLoadLensShortenPathTest,
	"LoadLens.Logic.ShortenPathCutsTheMiddleAndKeepsTheAssetName",
	LoadLensTests::TestFlags)

bool FLoadLensShortenPathTest::RunTest(const FString& Parameters)
{
	const FString Short = TEXT("/Game/BP_Rifle");
	TestEqual(TEXT("A path that fits is returned untouched"), ULoadLensStatics::ShortenPath(Short, 46), Short);

	const FString Long = TEXT("/Game/Weapons/Rifles/Assault/Attachments/Scopes/BP_LongRangeScope");
	const FString Cut = ULoadLensStatics::ShortenPath(Long, 46);

	TestTrue(TEXT("The asset name survives whole"), Cut.EndsWith(TEXT("/BP_LongRangeScope")));
	TestTrue(TEXT("The front of the path survives"), Cut.StartsWith(TEXT("/Game/")));
	TestTrue(TEXT("The cut is marked"), Cut.Contains(TEXT("...")));
	TestTrue(TEXT("It is shorter than what went in"), Cut.Len() < Long.Len());
	TestEqual(TEXT("And it fits the width it was given"), Cut.Len(), 46);

	// An asset name longer than the whole budget comes back longer than the budget, on purpose: a
	// truncated name cannot be pasted into a search field, and that is what the name is for.
	const FString HugeName = TEXT("/Game/A/B/BP_ThisIsAnAbsurdlyLongAssetNameThatNobodyShouldEverWrite");
	const FString HugeCut = ULoadLensStatics::ShortenPath(HugeName, 20);
	TestTrue(TEXT("The name is never truncated, even when it does not fit"),
		HugeCut.EndsWith(TEXT("/BP_ThisIsAnAbsurdlyLongAssetNameThatNobodyShouldEverWrite")));

	// A path with no slash at all - not a real package name, but it must not crash or come back empty.
	const FString NoSlash = ULoadLensStatics::ShortenPath(TEXT("AVeryLongNameWithoutAnySlashesInItAtAllWhatsoever"), 20);
	TestFalse(TEXT("A slashless string still comes back with something in it"), NoSlash.IsEmpty());

	return true;
}

//
// (6) SummarizeWorst. The sentence at the bottom of the box, which must never be blank.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLoadLensSummarizeWorstTest,
	"LoadLens.Logic.SummarizeWorstNamesTheThingToFixAndIsNeverEmpty",
	LoadLensTests::TestFlags)

bool FLoadLensSummarizeWorstTest::RunTest(const FString& Parameters)
{
	// A blank line at the bottom of the counter box looks like a bug in the tool, and somebody will
	// report it as one.
	TestEqual(TEXT("An empty table gets a clean sentence"),
		ULoadLensStatics::SummarizeWorst({}), FString(TEXT("no blocking loads")));

	TArray<FLoadLensRecord> Records;
	Records.Add(LoadLensTests::MakeRecord(TEXT("/Game/Cheap"), 1, 2.0f, 2.0f));

	FLoadLensRecord Expensive = LoadLensTests::MakeRecord(TEXT("/Game/Weapons/BP_Rifle"), 3, 96.3f, 41.8f);
	Expensive.Caller = TEXT("BP_Rifle_C::EquipWeapon");
	Records.Add(Expensive);

	const FString Sentence = ULoadLensStatics::SummarizeWorst(Records);
	TestTrue(TEXT("It names the package"), Sentence.Contains(TEXT("/Game/Weapons/BP_Rifle")));
	TestTrue(TEXT("It says how often"), Sentence.Contains(TEXT("3x")));
	TestTrue(TEXT("It says what it cost"), Sentence.Contains(TEXT("96.3 ms")));
	TestTrue(TEXT("It names who asked"), Sentence.Contains(TEXT("BP_Rifle_C::EquipWeapon")));

	// An import carries no time, so it can never win the sentence with a zero.
	TArray<FLoadLensRecord> OnlyImports;
	FLoadLensRecord Import = LoadLensTests::MakeRecord(TEXT("/Game/Imported"), 1, 0.0f, 0.0f);
	Import.bNestedOnly = true;
	OnlyImports.Add(Import);
	TestEqual(TEXT("A table of nothing but imports still reads clean"),
		ULoadLensStatics::SummarizeWorst(OnlyImports), FString(TEXT("no blocking loads")));

	// An upper bound is marked as one, so nobody quotes it as a measurement.
	TArray<FLoadLensRecord> Bounded;
	FLoadLensRecord Bound = LoadLensTests::MakeRecord(TEXT("/Game/Bounded"), 1, 18.0f, 18.0f);
	Bound.Timing = ELoadLensTiming::UpperBound;
	Bounded.Add(Bound);
	TestTrue(TEXT("An upper bound is printed as one"),
		ULoadLensStatics::SummarizeWorst(Bounded).Contains(TEXT("<18.0 ms")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
