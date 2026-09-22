// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LoadLensStatics.h"

#include "LoadLensRecorder.h"
#include "LoadLensSubsystem.h"

// --------------------------------------------------------------------------------------------------
// Pure logic
// --------------------------------------------------------------------------------------------------

TArray<FLoadLensRecord> ULoadLensStatics::RankRecords(const TArray<FLoadLensRecord>& Records)
{
	TArray<FLoadLensRecord> Ranked = Records;

	Ranked.Sort([](const FLoadLensRecord& A, const FLoadLensRecord& B)
	{
		if (A.TotalMilliseconds != B.TotalMilliseconds)
		{
			return A.TotalMilliseconds > B.TotalMilliseconds;
		}

		// Two packages that cost the same amount of time are ordered by how often they got through after
		// the warm-up: that is the one the budget is actually about.
		if (A.CountAfterWarmUp != B.CountAfterWarmUp)
		{
			return A.CountAfterWarmUp > B.CountAfterWarmUp;
		}

		if (A.Count != B.Count)
		{
			return A.Count > B.Count;
		}

		// The last tiebreak, and it is what makes the order total. Without it two rows level on every
		// count could swap places between one frame and the next, and a list that reorders itself while
		// somebody is reading it looks like the numbers are moving when they are not.
		return A.PackageName < B.PackageName;
	});

	return Ranked;
}

ELoadLensVerdict ULoadLensStatics::EvaluateBudget(int32 BlockingLoadCount, int32 Budget, int32 WarnLimit)
{
	const int32 SafeBudget = FMath::Max(Budget, 0);

	// A warning limit under the budget would describe a band that cannot exist. Raising it to the budget
	// turns that into the setting somebody clearly meant: no warning band at all, over budget is Over.
	const int32 SafeWarnLimit = FMath::Max(WarnLimit, SafeBudget);

	if (BlockingLoadCount <= SafeBudget)
	{
		return ELoadLensVerdict::Ok;
	}

	if (BlockingLoadCount <= SafeWarnLimit)
	{
		return ELoadLensVerdict::Warn;
	}

	return ELoadLensVerdict::Over;
}

int32 ULoadLensStatics::VerdictToExitCode(ELoadLensVerdict Verdict)
{
	switch (Verdict)
	{
	case ELoadLensVerdict::Warn:	return 1;
	case ELoadLensVerdict::Over:	return 2;
	default:						return 0;
	}
}

FString ULoadLensStatics::VerdictToString(ELoadLensVerdict Verdict)
{
	switch (Verdict)
	{
	case ELoadLensVerdict::Warn:	return TEXT("Warn");
	case ELoadLensVerdict::Over:	return TEXT("Over");
	default:						return TEXT("Ok");
	}
}

FString ULoadLensStatics::TimingToString(ELoadLensTiming Timing)
{
	switch (Timing)
	{
	case ELoadLensTiming::Exact:	return TEXT("exact");
	case ELoadLensTiming::Measured:	return TEXT("measured");
	default:						return TEXT("upper bound");
	}
}

FString ULoadLensStatics::SummarizeWorst(const TArray<FLoadLensRecord>& Records)
{
	const FLoadLensRecord* Worst = nullptr;

	for (const FLoadLensRecord& Record : Records)
	{
		// Imports carry no time of their own, so they can never be the worst finding - they would win
		// with a zero and push the real answer off the line.
		if (Record.bNestedOnly)
		{
			continue;
		}

		if (!Worst
			|| Record.TotalMilliseconds > Worst->TotalMilliseconds
			|| (Record.TotalMilliseconds == Worst->TotalMilliseconds && Record.PackageName < Worst->PackageName))
		{
			Worst = &Record;
		}
	}

	if (!Worst)
	{
		// Never an empty string. A blank line at the bottom of the counter box looks like a bug in the
		// tool, and somebody will report it as one.
		return TEXT("no blocking loads");
	}

	const TCHAR* Bound = Worst->Timing == ELoadLensTiming::UpperBound ? TEXT("<") : TEXT("");

	if (Worst->Count <= 1)
	{
		return FString::Printf(TEXT("%s blocked for %s%.1f ms - %s"),
			*Worst->PackageName, Bound, Worst->WorstMilliseconds, *Worst->Caller);
	}

	return FString::Printf(TEXT("%s blocked %dx for %s%.1f ms, worst %s%.1f ms - %s"),
		*Worst->PackageName, Worst->Count, Bound, Worst->TotalMilliseconds,
		Bound, Worst->WorstMilliseconds, *Worst->Caller);
}

FString ULoadLensStatics::ShortenPath(const FString& PackageName, int32 MaxLength)
{
	if (MaxLength <= 0 || PackageName.Len() <= MaxLength)
	{
		return PackageName;
	}

	// The asset name is the part somebody pastes into the content browser's search field, so it survives
	// whole even when that makes the line wider than asked for. A truncated name is worse than a wide row.
	int32 SlashIndex = INDEX_NONE;
	const FString Leaf = PackageName.FindLastChar(TEXT('/'), SlashIndex)
		? PackageName.Mid(SlashIndex)
		: PackageName;

	const int32 HeadLength = MaxLength - 3 - Leaf.Len();
	if (HeadLength <= 0)
	{
		return FString(TEXT("...")) + Leaf;
	}

	return PackageName.Left(HeadLength) + TEXT("...") + Leaf;
}

bool ULoadLensStatics::IsPathIgnored(const FString& PackageName, const TArray<FString>& IgnoredPathPrefixes)
{
	for (const FString& Prefix : IgnoredPathPrefixes)
	{
		// A blank row in a settings array is a typo, not an instruction to ignore the entire project.
		if (Prefix.IsEmpty())
		{
			continue;
		}

		if (PackageName.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool ULoadLensStatics::AccumulateRecord(
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
	int32 MaxTrackedPackages)
{
	if (PackageName.IsEmpty())
	{
		return false;
	}

	int32 Index = INDEX_NONE;

	if (const int32* Found = Lookup.Find(PackageName))
	{
		Index = *Found;
	}
	else
	{
		// The cap is the difference between a diagnostic tool and a memory leak. Refusing a new row here
		// is what lets the caller count the incident as untracked instead of pretending it never happened.
		if (MaxTrackedPackages > 0 && Records.Num() >= MaxTrackedPackages)
		{
			return false;
		}

		Index = Records.AddDefaulted();

		FLoadLensRecord& Fresh = Records[Index];
		Fresh.PackageName = PackageName;
		Fresh.Caller = Caller;
		Fresh.FirstFrame = FrameNumber;
		Fresh.FirstTimeSeconds = TimeSeconds;
		Fresh.Timing = Timing;

		Lookup.Add(PackageName, Index);
	}

	FLoadLensRecord& Record = Records[Index];

	++Record.Count;
	Record.LastFrame = FrameNumber;
	Record.LastTimeSeconds = TimeSeconds;
	Record.TotalMilliseconds += Milliseconds;

	if (Milliseconds > Record.WorstMilliseconds)
	{
		Record.WorstMilliseconds = Milliseconds;

		// The timing quality belongs to the figure it describes, so it follows the worst reading rather
		// than the latest one - that is the number the counter box and the report both print.
		Record.Timing = Timing;
	}

	if (bAfterWarmUp)
	{
		++Record.CountAfterWarmUp;
		Record.bDuringWarmUp = false;
	}

	if (!bNested)
	{
		// The first time a row that had only ever been an import turns out to be a real, top-level
		// blocking load, it takes that caller. "Import of something else" is the weaker answer and must
		// never overwrite the stronger one - which is also why this reads the flag before clearing it.
		if (Record.bNestedOnly)
		{
			Record.Caller = Caller;
		}

		Record.bNestedOnly = false;
	}

	return true;
}

FLoadLensSummary ULoadLensStatics::BuildSummary(
	const TArray<FLoadLensRecord>& Records,
	int32 Budget,
	int32 WarnLimit,
	int32 IgnoredCount,
	int32 UntrackedCount)
{
	FLoadLensSummary Summary;

	Summary.Budget = FMath::Max(Budget, 0);
	Summary.WarnLimit = FMath::Max(WarnLimit, Summary.Budget);
	Summary.IgnoredCount = IgnoredCount;
	Summary.UntrackedCount = UntrackedCount;
	Summary.UniquePackages = Records.Num();

	for (const FLoadLensRecord& Record : Records)
	{
		Summary.TotalCount += Record.Count;
		Summary.CountAfterWarmUp += Record.CountAfterWarmUp;
		Summary.TotalMilliseconds += Record.TotalMilliseconds;

		if (Record.CountAfterWarmUp > 0)
		{
			// The share of the time that happened once the map had settled. Not exact per incident - a
			// record holds a sum, not a list - so it is apportioned by how many of its loads counted.
			const float Share = static_cast<float>(Record.CountAfterWarmUp) / static_cast<float>(FMath::Max(Record.Count, 1));
			Summary.TotalMillisecondsAfterWarmUp += Record.TotalMilliseconds * Share;
		}

		if (Record.Timing == ELoadLensTiming::UpperBound)
		{
			Summary.bAnyUpperBound = true;
		}

		if (!Record.bNestedOnly && Record.WorstMilliseconds > Summary.WorstMilliseconds)
		{
			Summary.WorstMilliseconds = Record.WorstMilliseconds;
			Summary.WorstPackage = Record.PackageName;
			Summary.WorstTiming = Record.Timing;
		}
	}

	// The budget judges what got through after the warm-up. What happened while the map was still coming
	// up is on the screen and in the report, and it is deliberately not what fails a build.
	Summary.Verdict = EvaluateBudget(Summary.CountAfterWarmUp, Summary.Budget, Summary.WarnLimit);

	// The untracked ones never got a row, so the loop above could not see them. They are added to the
	// total because they happened, and deliberately not to the after-warm-up count: a record holds no
	// warm-up flag for a package it refused to create, and a budget must not be moved by a guess.
	Summary.TotalCount += UntrackedCount;

	return Summary;
}

// --------------------------------------------------------------------------------------------------
// Blueprint access to the live ledger
// --------------------------------------------------------------------------------------------------

ULoadLensSubsystem* ULoadLensStatics::GetLoadLens()
{
	return ULoadLensSubsystem::Get();
}

TArray<FLoadLensRecord> ULoadLensStatics::GetLoadLensRecords()
{
	return RankRecords(FLoadLensRecorder::Get().GetRecords());
}

FLoadLensSummary ULoadLensStatics::GetLoadLensSummary()
{
	return FLoadLensRecorder::Get().GetSummary();
}

int32 ULoadLensStatics::GetBlockingLoadCount()
{
	return FLoadLensRecorder::Get().GetSummary().CountAfterWarmUp;
}

bool ULoadLensStatics::IsOverBudget()
{
	return FLoadLensRecorder::Get().IsOverBudget();
}

void ULoadLensStatics::ResetLoadLens()
{
	FLoadLensRecorder::Get().Reset();
}

void ULoadLensStatics::SetLoadLensCounterBoxVisible(bool bVisible)
{
	FLoadLensRecorder::Get().SetShowCounterBox(bVisible);
}
