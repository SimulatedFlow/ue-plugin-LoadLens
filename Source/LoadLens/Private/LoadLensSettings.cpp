// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LoadLensSettings.h"

#include "LoadLensRecorder.h"

ULoadLensSettings::ULoadLensSettings()
{
	// The engine's own packages, and the script packages that are not files at all.
	//
	// A project cannot do anything about either, and a table that opens with forty rows nobody can act on
	// is a table nobody reads. Whatever this list drops is still counted in the summary as IgnoredCount
	// and printed in the counter box, so the list can never quietly hide its own effect.
	IgnoredPathPrefixes = {
		TEXT("/Engine/"),
		TEXT("/Script/"),
	};
}

FName ULoadLensSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}

FName ULoadLensSettings::GetSectionName() const
{
	return FName(TEXT("LoadLens"));
}

const ULoadLensSettings& ULoadLensSettings::Get()
{
	const ULoadLensSettings* Settings = GetDefault<ULoadLensSettings>();
	check(Settings);
	return *Settings;
}

#if WITH_EDITOR
void ULoadLensSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Changing the budget in the Details panel has to move the box on the next frame, not on the next
	// restart. A settings page whose effect you cannot see is a settings page people stop trusting.
	FLoadLensRecorder::Get().ApplySettings();
}
#endif
