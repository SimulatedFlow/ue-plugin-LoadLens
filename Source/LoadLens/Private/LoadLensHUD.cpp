// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LoadLensHUD.h"

#include "LoadLensSubsystem.h"

void ALoadLensHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bDrawCounterBox)
	{
		return;
	}

	if (ULoadLensSubsystem* LoadLens = ULoadLensSubsystem::Get())
	{
		LoadLens->DrawCounterBox(Canvas);
	}
}
