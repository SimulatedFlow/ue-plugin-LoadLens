// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LoadLensTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "LoadLensSubsystem.generated.h"

class UCanvas;

/**
 * Fired once a blocking load has been measured.
 *
 * Milliseconds is the stall; Timing says what that figure is worth. Bind it to put your own marker in
 * your own telemetry - LoadLens deliberately does not send anything anywhere.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLoadLensBlockingLoadSignature,
	const FString&, PackageName, float, Milliseconds, ELoadLensTiming, Timing);

/**
 * The Blueprint-facing face of the ledger, and the thing that draws the counter box.
 *
 * An engine subsystem, not a world subsystem, and that is the point: blocking loads happen between two
 * worlds as well as inside one, and during a map change they happen more than anywhere else. A ledger
 * that is torn down and rebuilt with the world would miss precisely the worst moment in the run.
 *
 * The subsystem owns none of the measuring. FLoadLensRecorder does that, from PreEarlyLoadingScreen
 * onwards, long before any subsystem exists; this class arrives later and finds the ledger already
 * running.
 */
UCLASS(DisplayName = "LoadLens")
class LOADLENS_API ULoadLensSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/** The one LoadLens subsystem, or null before the engine is up. */
	static ULoadLensSubsystem* Get();

	// --------------------------------------------------------------------------------------------------
	// The ledger
	// --------------------------------------------------------------------------------------------------

	/** Every package that has blocked, ranked worst first. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	TArray<FLoadLensRecord> GetRecords() const;

	/** The totals and the verdict. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	FLoadLensSummary GetSummary() const;

	/** Blocking loads after the warm-up window. The number the budget judges. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	int32 GetBlockingLoadCount() const;

	/** True when more got through than the budget allows. */
	UFUNCTION(BlueprintPure, Category = "LoadLens")
	bool IsOverBudget() const;

	/** Throw the ledger away and start counting again. The warm-up clock is not restarted. */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	void Reset();

	/**
	 * Write the ledger as JSON.
	 *
	 * An empty path means the one from the project settings, which is the same file LoadLens.Gate writes,
	 * so a developer and a build server end up looking at the same document.
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadLens", meta = (AdvancedDisplay = "Path"))
	bool WriteReport(const FString& Path);

	/** Write every row to the log - the whole table, not just the five the counter box shows. */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	void DumpToLog() const;

	/** How many blocking loads are allowed after the warm-up. */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	void SetBudget(int32 InBudget);

	UFUNCTION(BlueprintPure, Category = "LoadLens")
	int32 GetBudget() const;

	/** Show or hide the counter box. */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	void SetShowCounterBox(bool bInShow);

	UFUNCTION(BlueprintPure, Category = "LoadLens")
	bool IsShowingCounterBox() const;

	/**
	 * Draw the counter box on a canvas.
	 *
	 * ALoadLensHUD calls this for you. Call it from your own AHUD::DrawHUD instead if your project
	 * already has a HUD class - that is one line, and it means nobody has to choose between their HUD
	 * and this one.
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadLens")
	void DrawCounterBox(UCanvas* Canvas);

	/** Fired once a blocking load has been measured and folded into its record. */
	UPROPERTY(BlueprintAssignable, Category = "LoadLens")
	FLoadLensBlockingLoadSignature OnBlockingLoad;

private:
	void HandleBlockingLoadMeasured(const FLoadLensRecord& Record, float Milliseconds);

	FDelegateHandle MeasuredHandle;
};
