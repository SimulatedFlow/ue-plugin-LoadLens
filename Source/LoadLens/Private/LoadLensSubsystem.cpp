// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LoadLensSubsystem.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GlobalRenderResources.h"
#include "LoadLensLog.h"
#include "LoadLensRecorder.h"
#include "LoadLensStatics.h"
#include "Misc/StringBuilder.h"
#include "SceneTypes.h"

void ULoadLensSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The recorder has been running since PreEarlyLoadingScreen. Nothing is started here - this only
	// hangs the Blueprint-facing delegate onto a ledger that already has entries in it.
	MeasuredHandle = FLoadLensRecorder::Get().OnBlockingLoadMeasured.AddUObject(
		this, &ULoadLensSubsystem::HandleBlockingLoadMeasured);
}

void ULoadLensSubsystem::Deinitialize()
{
	// The recorder outlives this object by design, so an unbound handle here would be a dangling raw
	// pointer in a static delegate - the classic way an engine subsystem crashes on shutdown.
	FLoadLensRecorder::Get().OnBlockingLoadMeasured.Remove(MeasuredHandle);
	MeasuredHandle.Reset();

	Super::Deinitialize();
}

ULoadLensSubsystem* ULoadLensSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<ULoadLensSubsystem>() : nullptr;
}

void ULoadLensSubsystem::HandleBlockingLoadMeasured(const FLoadLensRecord& Record, float Milliseconds)
{
	OnBlockingLoad.Broadcast(Record.PackageName, Milliseconds, Record.Timing);
}

// --------------------------------------------------------------------------------------------------
// The ledger
// --------------------------------------------------------------------------------------------------

TArray<FLoadLensRecord> ULoadLensSubsystem::GetRecords() const
{
	return ULoadLensStatics::RankRecords(FLoadLensRecorder::Get().GetRecords());
}

FLoadLensSummary ULoadLensSubsystem::GetSummary() const
{
	return FLoadLensRecorder::Get().GetSummary();
}

int32 ULoadLensSubsystem::GetBlockingLoadCount() const
{
	return FLoadLensRecorder::Get().GetSummary().CountAfterWarmUp;
}

bool ULoadLensSubsystem::IsOverBudget() const
{
	return FLoadLensRecorder::Get().IsOverBudget();
}

void ULoadLensSubsystem::Reset()
{
	FLoadLensRecorder::Get().Reset();
}

bool ULoadLensSubsystem::WriteReport(const FString& Path)
{
	return FLoadLensRecorder::Get().WriteReport(Path);
}

void ULoadLensSubsystem::DumpToLog() const
{
	FLoadLensRecorder::Get().DumpToLog();
}

void ULoadLensSubsystem::SetBudget(int32 InBudget)
{
	FLoadLensRecorder::Get().SetBudget(InBudget);
}

int32 ULoadLensSubsystem::GetBudget() const
{
	return FLoadLensRecorder::Get().GetBudget();
}

void ULoadLensSubsystem::SetShowCounterBox(bool bInShow)
{
	FLoadLensRecorder::Get().SetShowCounterBox(bInShow);
}

bool ULoadLensSubsystem::IsShowingCounterBox() const
{
	return FLoadLensRecorder::Get().IsShowingCounterBox();
}

// --------------------------------------------------------------------------------------------------
// The counter box
// --------------------------------------------------------------------------------------------------

void ULoadLensSubsystem::DrawCounterBox(UCanvas* Canvas)
{
	const FLoadLensRecorder& Recorder = FLoadLensRecorder::Get();

	if (!Canvas || !Recorder.IsShowingCounterBox())
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const FLoadLensSummary& Summary = Recorder.GetSummary();
	const TArray<FLoadLensRecord> Ranked = ULoadLensStatics::RankRecords(Recorder.GetRecords());
	const int32 ListedPaths = FMath::Min(Recorder.GetTopPathLines(), Ranked.Num());

	constexpr float LineHeight = 15.0f;
	constexpr float BoxWidth = 620.0f;

	const float BoxX = static_cast<float>(Recorder.GetCounterBoxPosition().X);
	const float BoxY = static_cast<float>(Recorder.GetCounterBoxPosition().Y);

	// Header, worst-finding line, column head, the rows, and up to two footnotes.
	const int32 FootnoteLines = (Summary.IgnoredCount > 0 ? 1 : 0) + (Summary.UntrackedCount > 0 ? 1 : 0)
		+ (Summary.bAnyUpperBound ? 1 : 0);
	const int32 LineCount = 3 + ListedPaths + FootnoteLines + 1;

	FCanvasTileItem Background(
		FVector2D(BoxX - 8.0f, BoxY - 8.0f),
		GWhiteTexture,
		FVector2D(BoxWidth, LineCount * LineHeight + 16.0f),
		FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	float LineY = BoxY;

	auto DrawLine = [&](FStringView Line, const FLinearColor& Colour)
	{
		FCanvasTextStringViewItem Item(FVector2D(BoxX, LineY), Line, Font, Colour);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	const FLinearColor Good(0.55f, 0.95f, 0.55f, 1.0f);
	const FLinearColor Warn(1.0f, 0.78f, 0.30f, 1.0f);
	const FLinearColor Over(1.0f, 0.42f, 0.38f, 1.0f);
	const FLinearColor Body(0.90f, 0.90f, 0.90f, 1.0f);
	const FLinearColor Faint(0.62f, 0.62f, 0.66f, 1.0f);

	// Green means nothing got through after the warm-up. It is the only state worth being green about,
	// which is why the colour follows the verdict and not the total.
	const FLinearColor VerdictColour =
		Summary.Verdict == ELoadLensVerdict::Over ? Over :
		Summary.Verdict == ELoadLensVerdict::Warn ? Warn : Good;

	TStringBuilder<320> Line;

	// The header. This is the line a store screenshot is built around, so it carries the four numbers
	// that matter and nothing else.
	Line.Reset();
	Line.Appendf(TEXT("blocking loads %d (%d after warm-up)"), Summary.TotalCount, Summary.CountAfterWarmUp);

	if (!Summary.WorstPackage.IsEmpty())
	{
		Line.Appendf(TEXT(" | worst %s %s%.1f ms"),
			*ULoadLensStatics::ShortenPath(Summary.WorstPackage, Recorder.GetMaxPathLength()),
			Summary.WorstTiming == ELoadLensTiming::UpperBound ? TEXT("<") : TEXT(""),
			Summary.WorstMilliseconds);
	}

	Line.Appendf(TEXT(" | total %.1f ms"), Summary.TotalMilliseconds);
	DrawLine(Line.ToView(), VerdictColour);

	// The state of the warm-up, spelled out. Somebody looking at a green box needs to know whether it is
	// green because nothing blocked or green because nothing counts yet.
	Line.Reset();
	if (Summary.SecondsSinceMapLoad < 0.0f)
	{
		Line.Append(TEXT("starting up - nothing counts against the budget yet"));
	}
	else if (!Summary.bWarmUpOver)
	{
		Line.Appendf(TEXT("warm-up: %.1f s of %.1f s - nothing counts against the budget yet"),
			Summary.SecondsSinceMapLoad, Recorder.GetWarmUpSeconds());
	}
	else
	{
		Line.Appendf(TEXT("budget %d, warning limit %d, verdict %s"),
			Summary.Budget, Summary.WarnLimit, *ULoadLensStatics::VerdictToString(Summary.Verdict));
	}
	DrawLine(Line.ToView(), Summary.bWarmUpOver ? VerdictColour : Faint);

	Line.Reset();
	Line.Appendf(TEXT("%-46s %5s %10s %10s"), TEXT("package"), TEXT("count"), TEXT("total ms"), TEXT("worst ms"));
	DrawLine(Line.ToView(), Faint);

	for (int32 Index = 0; Index < ListedPaths; ++Index)
	{
		const FLoadLensRecord& Record = Ranked[Index];

		Line.Reset();
		Line.Appendf(TEXT("%-46s %5d %10.1f %s%9.1f"),
			*ULoadLensStatics::ShortenPath(Record.PackageName, Recorder.GetMaxPathLength()),
			Record.Count,
			Record.TotalMilliseconds,
			Record.Timing == ELoadLensTiming::UpperBound ? TEXT("<") : TEXT(" "),
			Record.WorstMilliseconds);

		if (Record.bNestedOnly)
		{
			Line.Append(TEXT("  import"));
		}
		else if (Record.CountAfterWarmUp == 0)
		{
			Line.Append(TEXT("  warm-up"));
		}

		// A row is coloured by what it contributes, not by the project's verdict: the one that got
		// through after the warm-up is the one somebody has to go and look at.
		DrawLine(Line.ToView(), Record.CountAfterWarmUp > 0 ? VerdictColour : Body);
	}

	if (Summary.IgnoredCount > 0)
	{
		Line.Reset();
		Line.Appendf(TEXT("%d load(s) dropped by the ignore list - not counted"), Summary.IgnoredCount);
		DrawLine(Line.ToView(), Faint);
	}

	if (Summary.UntrackedCount > 0)
	{
		Line.Reset();
		Line.Appendf(TEXT("%d load(s) counted but not named - the table is at its cap"), Summary.UntrackedCount);
		DrawLine(Line.ToView(), Faint);
	}

	if (Summary.bAnyUpperBound)
	{
		DrawLine(TEXT("'<' = upper bound: the load ended somewhere before that figure, not after it"), Faint);
	}

	// The sentence. Worth more than the table above it, because it names the thing to go and fix.
	DrawLine(ULoadLensStatics::SummarizeWorst(Recorder.GetRecords()), VerdictColour);
}
