// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LoadLensHUD.generated.h"

/**
 * The default host for the counter box.
 *
 * Set this as the HUD class of your GameMode and the box appears with no further wiring. It is a thin
 * actor on purpose - the drawing lives in the subsystem - so a project that already has a HUD class does
 * not have to choose between its own and this one: one call to ULoadLensSubsystem::DrawCounterBox from
 * your own DrawHUD gets the identical box.
 */
UCLASS(Blueprintable, DisplayName = "LoadLens HUD")
class LOADLENS_API ALoadLensHUD : public AHUD
{
	GENERATED_BODY()

public:
	//~ Begin AHUD interface
	virtual void DrawHUD() override;
	//~ End AHUD interface

	/** Draw the counter box. Off if something else in your project draws it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoadLens")
	bool bDrawCounterBox = true;
};
