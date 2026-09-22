// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class LoadLens : ModuleRules
{
	public LoadLens(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module, and deliberately no editor module.
		//
		// The hitch this plugin exists for is a hitch the player gets and the developer does not: the first
		// LoadSynchronous of a weapon on a slow disk. So everything here has to survive cooking and has to
		// work in a packaged Shipping build - which is also why the counter box is drawn on UCanvas from an
		// AHUD instead of in UMG. A widget tree would have to be cooked, referenced and kept alive by the
		// project; a Canvas box works in a level that contains nothing but a floor.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",

			// CoreUObject: the engine's hook lives on FCoreDelegates, but everything that makes a reading
			// honest - the sync-load nesting counter, the Blueprint script stack, the object currently being
			// serialised - lives on FUObjectThreadContext and FBlueprintContextTracker.
			"CoreUObject",

			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// RenderCore: GWhiteTexture, the background tile behind the counter box.
			"RenderCore",

			// Json: LoadLens.Gate writes Saved/LoadLens/report.json for the build server.
			"Json",
			"JsonUtilities",
		});

		// Deliberately NOT here: UnrealEd, UMG, Slate.
		//
		// Unreal Insights already exists and is a better recorder than anything this plugin could be. What
		// it is not is a thing that is on the screen while somebody plays the game, in the build they will
		// ship, with a number a producer can read. That is the whole gap, and closing it means not linking
		// a single editor-only module.
	}
}
