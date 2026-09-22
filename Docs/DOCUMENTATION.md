# LoadLens - Documentation

**LoadLens does not preload anything and does not fix anything. It tells you what blocked and what it
cost.**

That sentence is first on purpose. Everything below describes a measuring instrument, not a solution: the
fix for a blocking load is always a change in your project, and LoadLens exists so that change is aimed
at the right line.

- **Engine:** Unreal Engine **5.8**
- **Platform:** Windows (Win64), Development and Shipping, editor and packaged game
- **Modules:** one runtime module (`LoadLens`), no editor module, no UMG, no Slate
- **Online copy of this page:** <https://wiki.teufel-engineering.com/en/LoadLens/documentation>
- **Support:** <mailto:teufelsilvan@gmail.com>

---

## Contents

1. [Installation](#installation)
2. [Quick start](#quick-start)
3. [Supported engine and platforms](#supported-engine-and-platforms)
4. [The demo map](#the-demo-map)
5. [How it works](#how-it-works)
6. [The counter box](#the-counter-box)
7. [The console commands](#the-console-commands)
8. [The gate, and the report](#the-gate-and-the-report)
9. [Project settings](#project-settings)
10. [Class overview](#class-overview)
11. [Blueprint API](#blueprint-api)
12. [C++ API and code examples](#c-api-and-code-examples)
13. [What the plugin does **not** do](#what-the-plugin-does-not-do)
14. [Why some loads show no caller](#why-some-loads-show-no-caller)
15. [What the millisecond figures are worth](#what-the-millisecond-figures-are-worth)
16. [Automated tests](#automated-tests)
17. [Troubleshooting](#troubleshooting)

---

## Installation

**From Fab / the Epic Games Launcher**

1. Install LoadLens for Unreal Engine 5.8 from the Launcher's *Library > Fab Library*.
2. Open your project, go to *Edit > Plugins > Engine Tools*, enable **LoadLens**, restart the editor.

**As a project plugin (source)**

1. Copy the `LoadLens` folder into your project's `Plugins` directory, so that
   `<YourProject>/Plugins/LoadLens/LoadLens.uplugin` exists.
2. Right-click your `.uproject` and *Generate Visual Studio project files* (C++ projects), or just start
   the editor and let it compile the module when it asks.
3. Restart the editor; enable **LoadLens** under *Edit > Plugins > Engine Tools* if it is not on already.

A Blueprint-only project works too: the plugin ships with source and the launcher build ships with
binaries, and nothing in LoadLens needs a C++ class in your project.

The measuring starts before the first map loads, whether or not anything is drawing. The only thing you
have to wire up is the counter box, and only if you want to see it on screen.

---

## Quick start

Four steps, about a minute:

1. **Enable the plugin** and restart the editor.
2. **Show the counter box.** Set your GameMode's *HUD Class* to **LoadLens HUD**.
   If your project already has a HUD class, do not replace it - add one line to your own `DrawHUD`
   instead:

   ```cpp
   // MyHUD.cpp
   #include "LoadLensSubsystem.h"

   void AMyHUD::DrawHUD()
   {
       Super::DrawHUD();

       if (ULoadLensSubsystem* LoadLens = ULoadLensSubsystem::Get())
       {
           LoadLens->DrawCounterBox(Canvas);
       }
   }
   ```

   In Blueprint the same thing is one node: *Event Receive Draw HUD* → **Get Load Lens** →
   **Draw Counter Box** (Canvas from the event).
3. **Play, and wait three seconds** for the warm-up window to close. The box is green and says so.
4. **Type `LoadLens.Provoke` in the console.** The box turns amber, names the package, prints the
   milliseconds and the Blueprint function that asked for it. That is the whole tool, working, on a load
   you triggered on purpose.

Then run your own game for a while and watch what turns up. When you want the same answer in writing:

```
LoadLens.Report
```

writes `Saved/LoadLens/report.json`. On a build server, use `LoadLens.Gate 60` instead - it measures,
reports and sets the process exit code. See [The gate, and the report](#the-gate-and-the-report).

---

## Supported engine and platforms

| | |
| --- | --- |
| **Engine version** | Unreal Engine 5.8 (`"EngineVersion": "5.8.0"`) |
| **Supported development platforms** | Windows (Win64) |
| **Supported target build platforms** | Windows (Win64) |
| **Build configurations** | Development and Shipping, editor and packaged game. The counter box, the console commands, the gate and the JSON report all work in a packaged Shipping build. |
| **Module** | `LoadLens`, type `Runtime`, `LoadingPhase` `PreEarlyLoadingScreen`, `PlatformAllowList: ["Win64"]` |
| **Dependencies** | Engine modules only: `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `RenderCore`, `Json`, `JsonUtilities`. No third-party code, no other marketplace plugin. |
| **Network replication** | None. LoadLens measures the process it runs in. |
| **Blueprint support** | Full: a static function library, an assignable event, and settings in *Project Settings*. |

The module deliberately does **not** link `UnrealEd`, `UMG` or `Slate`. The hitch this plugin exists for
is one the player gets and the developer does not, so everything in it has to survive cooking. That is
also why the counter box is drawn on `UCanvas` from an `AHUD` rather than built in UMG: a widget tree
would have to be cooked, referenced and kept alive by your project, while a Canvas box works in a level
that contains nothing but a floor.

Other platforms are not listed because they are not tested. The code itself uses no Windows-specific API;
if you build for another platform yourself, the hook, the counting, the report and the gate are portable,
and the only thing you would be verifying is the timing model described in
[What the millisecond figures are worth](#what-the-millisecond-figures-are-worth).

---

## The demo map

`Content/LoadLens/Maps/L_LoadLensDemo`.

Open it and press Play. Six panels stand on pedestals, each one showing *NOT LOADED*. Behind every panel
is a different 2K texture that is cold on disk and referenced by nothing, so loading it is a real load,
not a staged one. The panel on the right of the screen has the buttons:

- **SYNC LOAD** resolves the next texture with **`LoadAsset_Blocking`**. The panel lights up, and the
  counter box on the left gains a row with the package name, the milliseconds it cost and the Blueprint
  function that asked for it. Past the budget the box turns amber, past the warning limit it turns red.
- **ASYNC LOAD** resolves the *same kind of asset* asynchronously. The panel lights up exactly the same
  way and the counter box does not move at all. That side-by-side is the demo.
- **PROVOKE** runs `LoadLens.Provoke`, which blocks on the plugin's own heavy texture.
- **WRITE JSON REPORT** writes `Saved/LoadLens/report.json` and dumps the whole table to the log.
- **RESET** clears the ledger, drops the dynamic materials that hold the loaded textures and collects
  garbage, so the panels are cold again. A package still in memory cannot block a second time - which is
  itself worth knowing, and is why the button exists.

The numbers are real measurements taken on the machine you are sitting at, not values baked into the map
- which is also why the millisecond figure differs between a fast NVMe and a slow drive. That difference
is the entire point of the plugin. On the machine the shipped screenshots were taken on - an NVMe drive -
three sync loads came to `total 7.5 ms` with a worst of `3.3 ms`; six came to `13.3 ms`. On a hard disk,
or with a package larger than a texture, the same six loads are an order of magnitude worse.

The demo GameMode uses `ALoadLensHUD`, so the counter box is on from the first frame.

**One caveat about play-in-editor.** The editor keeps every asset it has ever loaded in memory, so a
texture the content browser has already thumbnailed cannot block a second time - and the warm-up clock
starts on `PostLoadMapWithWorld`, which a packaged game and a `-game` run fire and PIE does not. Run the
demo map as a **Standalone Game** (*Play > Standalone Game*, or a packaged build) to see the demo textures
actually block and the budget verdict turn over. In PIE the hook still records, but the assets are already
warm and the box stays inside the warm-up window.

Demo content, all of it under the plugin's own pack folder:

| Asset | What it is |
|---|---|
| `Content/LoadLens/Maps/L_LoadLensDemo` | The demo map. |
| `Content/LoadLens/Blueprints/BP_LoadLensDemoGameMode` | Demo GameMode; HUD class is `BP_LoadLensDemoHUD`. |
| `Content/LoadLens/Blueprints/BP_LoadLensDemoHUD` | `ALoadLensHUD` child; also creates the demo panel. |
| `Content/LoadLens/Blueprints/BP_LoadLensDemoStage` | The six panels and the sync/async/reset logic. |
| `Content/LoadLens/UI/WBP_LoadLensDemoPanel` | The button panel. |
| `Content/LoadLens/Assets/T_LoadLensHeavy_01..06` | The six cold 2K textures. |
| `Content/LoadLens/Assets/T_LoadLensHeavy` | What `LoadLens.Provoke` loads by default. |
| `Content/LoadLens/Assets/T_LoadLensSlotEmpty` | The *NOT LOADED* placeholder. |
| `Content/LoadLens/Materials/M_LoadLensScreen` | Panel material, texture parameter `BaseTexture`. |
| `Content/LoadLens/Materials/M_LoadLensFloor`, `M_LoadLensPedestal` | Set dressing. |

---

## How it works

The engine broadcasts `FCoreDelegates::OnSyncLoadPackage` from `LoadPackageInternal`, on the game
thread, at the start of every blocking load and before the flush that does the blocking. It carries the
package name.

LoadLens hangs on that hook at **PreEarlyLoadingScreen** - before the game starts loading its first map,
because otherwise the most interesting loads happen with nobody listening. Around the hook it records:

- the long package name
- the time the load started, and how long the stall lasted (see
  [What the millisecond figures are worth](#what-the-millisecond-figures-are-worth))
- the frame number and the seconds since the map finished loading
- whether it happened during the warm-up window
- who asked, or `unknown caller` (see [Why some loads show no caller](#why-some-loads-show-no-caller))

**It counts instead of spamming.** The same path loaded a hundred times is one row with a counter and a
sum, not a hundred log lines. A log that writes a line per incident makes the hitch it is measuring
worse, which is the fastest way for a profiling tool to become the problem it was installed to find.
There is a project setting to turn per-incident logging on anyway, for the afternoon when you are chasing
one specific load; it is off by default and the comment saying why sits next to the code.

**Nested loads carry no time of their own.** A package pulled in as an import underneath another
synchronous load is listed, marked `import`, and contributes zero milliseconds - its time is already
inside the outer load's figure, and counting it twice would inflate the one number people quote at each
other in a review. LoadLens tells nesting apart from a fresh load by reading the engine's own
`SyncLoadUsingAsyncLoaderCount`, not by guessing from timing.

**The warm-up.** A map load always brings blocking loads with it - the level's assets, the game mode, the
HUD. Counting those would make the box red every time somebody presses Play, and a tool that is always
red is a tool people switch off in the first week. So the budget only starts counting once a map has
finished loading and the warm-up window (three seconds by default) has passed. Warm-up loads are still
listed, still timed, and marked `warm-up`.

---

## The counter box

This is the real box from the demo map, after six blocking loads:

```
blocking loads 6 (6 after warm-up) | worst /LoadLens/LoadLens/Assets/T_LoadLensHeavy_02 3.3 ms | total 13.3 ms
budget 0, warning limit 5, verdict Over
package                                        count   total ms   worst ms
/LoadLens/LoadLens/Assets/T_LoadLensHeavy_02       1        3.3        3.3
/LoadLens/LoadLens/Assets/T_LoadLensHeavy_03       1        2.2        2.2
/LoadLens/LoadLens/Assets/T_LoadLensHeavy_04       1        2.1        2.1
/LoadLens/LoadLens/Assets/T_LoadLensHeavy_05       1        2.1        2.1
/LoadLens/LoadLens/Assets/T_LoadLensHeavy_06       1        2.0        2.0
/LoadLens/LoadLens/Assets/T_LoadLensHeavy_02 blocked for 3.3 ms - BP_LoadLensDemoStage_C::ExecuteUbergraph_BP_LoadLensDemoStage
```

In a real project the rows carry your own paths, your own callers, and extra marks where they apply:

```
/Game/Weapons/BP_Rifle                             3       96.3       41.8
/Game/Audio/SC_RifleFire                           1        4.9        4.9  warm-up
/Game/Weapons/M_RifleBody                          1        0.0        0.0  import
```

- **Green** - nothing got through after the warm-up. The only state worth being green about.
- **Amber** - over budget, still at or under the warning limit.
- **Red** - above the warning limit.

While the warm-up is still running the second line says so, in words, instead of showing a budget. A
green box needs to distinguish between "nothing blocked" and "nothing counts yet"; leaving that
ambiguous is how a tool gets trusted for the wrong reason.

The bottom line is a sentence rather than a number, and it is the most useful thing in the box: it names
the package, how often it blocked, what it cost and who asked. It is never blank - an empty table gets
`no blocking loads`, because a blank line at the bottom of a debug box looks like a bug in the tool and
somebody will report it as one.

Long paths are shortened in the middle. The asset name is never shortened, because that is the part you
paste into the content browser's search field.

It is drawn on `UCanvas` from an `AHUD`. No UMG, no Slate, no editor module - so it is still there in a
packaged Shipping build, which is where these loads actually cost somebody something.

---

## The console commands

| Command | What it does |
| --- | --- |
| `LoadLens.Show [0\|1]` | Show the counter box. |
| `LoadLens.Hide` | Hide it. |
| `LoadLens.Dump` | Write the whole table to the log - every row, not just the five on screen. |
| `LoadLens.Reset` | Throw the ledger away and start counting again. |
| `LoadLens.Budget <n> [warn]` | Blocking loads allowed after the warm-up, and the warning limit. With no arguments, prints the current ones. |
| `LoadLens.Report [path]` | Write the ledger as JSON. Default: the path from the project settings. |
| `LoadLens.Gate <seconds> [-noexit]` | Measure, write the report, exit 0 / 1 / 2. |
| `LoadLens.Provoke [asset]` | Load an asset synchronously, on purpose. |

### `LoadLens.Provoke`

It exists so you can watch the box react without first having to build a bug into your own game. It
performs a real `StaticLoadObject` on the game thread and the number it produces is a real measurement -
there is nothing staged about it.

If the asset is already in memory it cannot block, and the command says so instead of reporting a zero
that looks like a broken plugin. Point it at something cold:

```
LoadLens.Provoke /Game/Weapons/BP_Rifle.BP_Rifle
```

---

## The gate, and the report

```
LoadLens.Gate 60
```

Measures for sixty seconds, writes `Saved/LoadLens/report.json` and ends the process with:

| Exit code | Meaning |
| --- | --- |
| `0` | At or under the budget. With the default budget of zero: nothing blocked after the warm-up. |
| `1` | Over budget, at or under the warning limit. |
| `2` | Above the warning limit. |

The same three numbers as LocaleGuard, AssetWarden and WidgetLedger, meaning the same three things, so a
project that owns more than one of these does not have to keep two conventions in its head.

`-noexit` measures and reports without ending the process, which is what you want when you are typing
the command into the console rather than running it from a build script.

The gate clears the ledger when it starts. It measures its own window: what happened while the map was
still coming up is not what the build server asked about, and leaving it in would fail builds for a hitch
nobody saw.

A run from a build script looks like this:

```
UnrealEditor-Cmd.exe MyProject.uproject /Game/Maps/L_Smoke -game -unattended -nullrhi ^
    -ExecCmds="LoadLens.Gate 60"
```

The log carries two greppable lines:

```
LOADLENS GATE RESULT=Warn loads=7 after_warmup=3 budget=0 warn=5 total_ms=96.3 exit=1
LOADLENS GATE WORST=/Game/Weapons/BP_Rifle blocked 3x for 96.3 ms, worst 41.8 ms - BP_Rifle_C::EquipWeapon
```

### The report

Written by `LoadLens.Gate`, `LoadLens.Report` and `ULoadLensSubsystem::WriteReport`. This is a real
report from the demo map, abbreviated to one package:

```json
{
  "plugin": "LoadLens",
  "reportVersion": 1,
  "verdict": "Over",
  "exitCode": 2,
  "budget": 0,
  "warnLimit": 5,
  "warmUpSeconds": 3,
  "warmUpOver": true,
  "blockingLoads": 6,
  "blockingLoadsAfterWarmUp": 6,
  "uniquePackages": 6,
  "totalMilliseconds": 13.3077,
  "totalMillisecondsAfterWarmUp": 13.3077,
  "worstPackage": "/LoadLens/LoadLens/Assets/T_LoadLensHeavy_02",
  "worstMilliseconds": 3.2637,
  "worstTiming": "exact",
  "ignoredLoads": 0,
  "untrackedLoads": 0,
  "anyUpperBound": false,
  "worstFinding": "/LoadLens/LoadLens/Assets/T_LoadLensHeavy_02 blocked for 3.3 ms - BP_LoadLensDemoStage_C::ExecuteUbergraph_BP_LoadLensDemoStage",
  "packages": [
    {
      "package": "/LoadLens/LoadLens/Assets/T_LoadLensHeavy_02",
      "count": 1,
      "countAfterWarmUp": 1,
      "totalMilliseconds": 3.2637,
      "worstMilliseconds": 3.2637,
      "timing": "exact",
      "firstFrame": 1724,
      "lastFrame": 1724,
      "firstTimeSeconds": 74.2909,
      "lastTimeSeconds": 74.2909,
      "caller": "BP_LoadLensDemoStage_C::ExecuteUbergraph_BP_LoadLensDemoStage",
      "duringWarmUp": false,
      "importOnly": false
    }
  ]
}
```

`reportVersion` is there so a script that parses this file can tell whether it still understands it.

---

## Project settings

*Project Settings > Plugins > LoadLens*, stored in `DefaultGame.ini`.

| Setting | Default | Notes |
| --- | --- | --- |
| **Warm Up Seconds** | `3.0` | Seconds after a map finishes loading during which nothing counts against the budget. Zero means everything counts from the first frame. |
| **Blocking Load Budget** | `0` | How many are allowed after the warm-up. Zero is not a placeholder: every synchronous load during play is a frame somebody sees stop. |
| **Warn Limit** | `5` | Above this count the verdict is `Over` instead of `Warn`. Set it equal to the budget to remove the warning band entirely. |
| **Ignored Path Prefixes** | `/Engine/`, `/Script/` | Prefix match, case-insensitive. What this drops is still reported as a count, so the list can never hide its own effect. |
| **Show Counter Box** | `true` | Same as `LoadLens.Show`. |
| **Top Path Lines** | `5` | Rows under the header. |
| **Counter Box Position** | `24, 90` | Top left corner, in pixels. |
| **Max Path Length** | `46` | Where a path gets shortened in the middle. The asset name is never shortened. |
| **Report Path** | `Saved/LoadLens/report.json` | Relative to the project directory. |
| **Log Each Blocking Load** | `false` | Off, and the reason is in the code: a line per incident makes the hitch worse. |
| **Max Tracked Packages** | `512` | Cap on distinct rows. Loads beyond it are counted but not named, and the summary reports how many. |
| **Provoke Asset Path** | `/LoadLens/LoadLens/Assets/T_LoadLensHeavy.T_LoadLensHeavy` | What `LoadLens.Provoke` loads. Point it at one of your own assets to see what it really costs cold. |

The settings are read on `OnPostEngineInit` and again whenever one is edited. They are read that late
because the hook is installed at `PreEarlyLoadingScreen`, long before there is a UObject system to read a
CDO from - anything caught in between is counted and timed against the defaults, and re-judged the moment
the real budget arrives.

Because this is a `UDeveloperSettings` with `Config = Game`, the values ship with your game in
`DefaultGame.ini`; there is nothing to set up at runtime and nothing editor-only about them.

---

## Class overview

Five public classes and two structs. Everything is in the `LoadLens` module and everything public is
exported with `LOADLENS_API`.

| Type | Header | What it is for |
| --- | --- | --- |
| `ULoadLensSubsystem` | `LoadLensSubsystem.h` | `UEngineSubsystem`. The Blueprint-facing face of the ledger: records, summary, reset, report, budget, counter box, and the `OnBlockingLoad` event. |
| `ULoadLensStatics` | `LoadLensStatics.h` | `UBlueprintFunctionLibrary`. The pure arithmetic (ranking, budget, folding, path shortening) plus static shortcuts into the live ledger. |
| `ALoadLensHUD` | `LoadLensHUD.h` | `AHUD`. The default host for the counter box. Blueprintable, one property: `bDrawCounterBox`. |
| `ULoadLensSettings` | `LoadLensSettings.h` | `UDeveloperSettings`. Everything in *Project Settings > Plugins > LoadLens*. |
| `FLoadLensRecorder` | `LoadLensRecorder.h` | Not a `UObject`. The measuring itself: installs the engine hook at `PreEarlyLoadingScreen`, times the stalls, keeps the table. It exists before the subsystem does, which is the point. |
| `FLoadLensRecord` | `LoadLensTypes.h` | One row: package, count, count after warm-up, total and worst milliseconds, first/last frame and time, caller, warm-up flag, import flag, timing quality. |
| `FLoadLensSummary` | `LoadLensTypes.h` | The totals and the verdict, plus the ignored and untracked counts and whether any figure is an upper bound. |
| `ELoadLensVerdict` | `LoadLensTypes.h` | `Ok` / `Warn` / `Over` - also the gate's exit codes 0 / 1 / 2. |
| `ELoadLensTiming` | `LoadLensTypes.h` | `Exact` / `Measured` / `UpperBound` - what a millisecond figure is worth. |

`ULoadLensSubsystem` is an **engine** subsystem, not a world subsystem. Blocking loads happen between two
worlds as well as inside one, and during a map change they happen more than anywhere else - a ledger torn
down and rebuilt with the world would miss precisely the worst moment in the run.

The subsystem owns none of the measuring. `FLoadLensRecorder` does that, from `PreEarlyLoadingScreen`
onwards, long before any subsystem exists; the subsystem arrives later and finds the ledger already
running.

---

## Blueprint API

Everything on `ULoadLensStatics` is static - no target pin, callable from any graph.

**Reading the live ledger**

| Node | Returns |
| --- | --- |
| **Get Load Lens** | The subsystem, or null before the engine is up. |
| **Get Load Lens Records** | `TArray<FLoadLensRecord>`, ranked worst first. |
| **Get Load Lens Summary** | `FLoadLensSummary` - totals, verdict, warm-up state. |
| **Get Blocking Load Count** | The number the budget judges. |
| **Is Over Budget** | `bool`. |

**Doing something about it**

| Node | Does |
| --- | --- |
| **Reset Load Lens** | Clears the ledger. |
| **Set Load Lens Counter Box Visible** | Same as `LoadLens.Show` / `.Hide`. |
| **Write Report** *(on the subsystem)* | Writes the JSON. Empty path means the one from the settings. |
| **Dump To Log** *(on the subsystem)* | The whole table to the log. |
| **Set Budget** / **Get Budget** *(on the subsystem)* | The budget, at runtime. |
| **Draw Counter Box** *(on the subsystem)* | Draw the box on a canvas you already have. |

**The pure logic**, usable on data of your own - these need no running game at all:

**Rank Records**, **Evaluate Budget**, **Verdict To Exit Code**, **Verdict To String**,
**Timing To String**, **Summarize Worst**, **Shorten Path**, **Is Path Ignored**.

**The event**

`ULoadLensSubsystem::OnBlockingLoad` is a `BlueprintAssignable` multicast delegate carrying
`(PackageName, Milliseconds, Timing)`. Bind it to put a marker in your own telemetry, your own on-screen
debug, or your own automated test. LoadLens deliberately does not send anything anywhere itself.

A typical Blueprint use, on *Event Begin Play*:

> **Get Load Lens** → *(Is Valid)* → **Bind Event to On Blocking Load** → custom event
> `HandleBlockingLoad (PackageName, Milliseconds, Timing)` → **Print String** /
> your analytics node.

---

## C++ API and code examples

Add the module to your own `Build.cs` if you want to call into it from C++:

```csharp
PrivateDependencyModuleNames.AddRange(new string[] { "LoadLens" });
```

### Draw the counter box from your own HUD

```cpp
#include "LoadLensSubsystem.h"

void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    if (ULoadLensSubsystem* LoadLens = ULoadLensSubsystem::Get())
    {
        LoadLens->DrawCounterBox(Canvas);
    }
}
```

### React to a blocking load as it happens

```cpp
// MyPerfWatcher.h
UCLASS()
class AMyPerfWatcher : public AActor
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void HandleBlockingLoad(const FString& PackageName, float Milliseconds, ELoadLensTiming Timing);
};
```

```cpp
// MyPerfWatcher.cpp
#include "LoadLensSubsystem.h"
#include "LoadLensStatics.h"

void AMyPerfWatcher::BeginPlay()
{
    Super::BeginPlay();

    if (ULoadLensSubsystem* LoadLens = ULoadLensSubsystem::Get())
    {
        LoadLens->OnBlockingLoad.AddDynamic(this, &AMyPerfWatcher::HandleBlockingLoad);
    }
}

void AMyPerfWatcher::HandleBlockingLoad(const FString& PackageName, float Milliseconds,
                                        ELoadLensTiming Timing)
{
    // Timing says what the number is worth: Exact, Measured, or UpperBound.
    UE_LOG(LogTemp, Warning, TEXT("Blocking load: %s %.1f ms (%s)"),
        *PackageName, Milliseconds, *ULoadLensStatics::TimingToString(Timing));
}
```

The delegate is a `DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams`, so the handler must be a `UFUNCTION`
and you bind with `AddDynamic`. Unbind with `RemoveDynamic` in `EndPlay` if your actor can outlive the
binding's usefulness - the subsystem outlives every world.

### Fail your own smoke test on a blocking load

```cpp
#include "LoadLensSubsystem.h"
#include "LoadLensStatics.h"

void AMySmokeTest::CheckLoads()
{
    ULoadLensSubsystem* LoadLens = ULoadLensSubsystem::Get();
    if (!LoadLens)
    {
        return;
    }

    const FLoadLensSummary Summary = LoadLens->GetSummary();
    if (Summary.Verdict != ELoadLensVerdict::Ok)
    {
        // SummarizeWorst names the thing to go and fix, in one sentence, and is never empty.
        const FString Finding = ULoadLensStatics::SummarizeWorst(LoadLens->GetRecords());

        UE_LOG(LogTemp, Error, TEXT("%d blocking loads after warm-up (%.1f ms). %s"),
            Summary.CountAfterWarmUp, Summary.TotalMillisecondsAfterWarmUp, *Finding);

        LoadLens->WriteReport(FString());   // empty = the path from the project settings
    }
}
```

### Walk the table yourself

```cpp
for (const FLoadLensRecord& Record : ULoadLensStatics::GetLoadLensRecords())   // already ranked
{
    if (Record.bNestedOnly)
    {
        continue;   // an import; its time is inside the load above it
    }

    UE_LOG(LogTemp, Log, TEXT("%-50s x%d  %.1f ms (worst %.1f) - %s"),
        *ULoadLensStatics::ShortenPath(Record.PackageName, 50),
        Record.Count, Record.TotalMilliseconds, Record.WorstMilliseconds, *Record.Caller);
}
```

### Judge a count without a running game

`EvaluateBudget` is pure and static, which is what makes the budget rule testable:

```cpp
// budget 0, warning limit 5 → 0 is Ok, 1..5 is Warn, 6+ is Over
const ELoadLensVerdict Verdict = ULoadLensStatics::EvaluateBudget(/*Count*/ 6, /*Budget*/ 0, /*Warn*/ 5);
const int32 ExitCode = ULoadLensStatics::VerdictToExitCode(Verdict);   // 2
```

---

## What the plugin does **not** do

- **It does not preload anything.** No asset manager, no bundle, no prewarm list. Whatever your project
  loads, it still loads.
- **It does not fix anything.** No load is converted, deferred, throttled or cancelled. Turning a
  `LoadSynchronous` into an async load is a change in your graph, and it has to be, because only you know
  what the code is allowed to do while the asset is not there yet.
- **It does not measure level streaming.** A streaming cell arriving late is a different hitch with a
  different cause. That is StreamGuard's subject. Most projects have both problems; they are complementary
  tools, not alternatives.
- **It does not replace Unreal Insights.** Insights is a better recorder and always will be. It is also a
  second program and a recording. LoadLens is a number on the screen in the build you ship.
- **It sends nothing anywhere.** No telemetry, no network, no analytics. The report is a file in your own
  `Saved` folder.
- **It does not name a caller it is not sure about.** See the next section.

---

## Why some loads show no caller

Some rows say `unknown caller`, and that is a deliberate answer rather than a missing one.

LoadLens establishes the caller in this order:

1. **The Blueprint stack.** If a Blueprint function is running when the load starts, the top frame gives
   the class and the function - `BP_Rifle_C::EquipWeapon`. This is the useful case and it covers most of
   what people actually hit, because most accidental `LoadSynchronous` calls are in Blueprint.
2. **The serialisation context.** If the load happened while another object was being serialised, that
   object is named - `serialising /Game/Maps/L_Arena.L_Arena:PersistentLevel.BP_Spawner_1`. Not the call
   site, but a real place to start looking.
3. **The import chain.** A package pulled in underneath another blocking load is marked
   `import of /Game/…`, naming the load that dragged it in.
4. **Nothing.** `unknown caller`.

The fourth case happens when native C++ calls `LoadObject` or resolves a soft pointer outside any script
frame. There is no portable way for a plugin to find out which function that was: the Blueprint stack is
empty because no Blueprint is running, and a native stack walk gives addresses rather than names in a
Shipping build, which is exactly the configuration where these loads matter most.

So the row says `unknown caller` and nothing else. **A wrong culprit costs more time than no culprit** -
a plausible-looking name sends somebody to the wrong file for an afternoon, and they will trust the next
name less afterwards. The package name and the frame number are still there, and `stat namedevents` or
Insights on that frame will finish the job in a minute.

---

## What the millisecond figures are worth

The engine reports the **start** of a synchronous load on every platform and in every build
configuration. It reports the **end** only in some. Rather than print a number that quietly means
something different depending on how the project was compiled, every row carries how its figure was
obtained, and the counter box marks the difference.

| Timing | How it was obtained | What it means |
| --- | --- | --- |
| `exact` | `FCoreUObjectDelegates::OnEndLoadPackage` fired for that package. | The real duration. Available in editor builds and in `-game` runs of an editor build. |
| `measured` | The stall was followed from inside the flush, through the async loader's progress callback, which fires on the game thread in every build including Shipping. | The time from the start of the load to the last progress signal inside it. It can fall a fraction of a millisecond short of the true end; it never overshoots. |
| `upper bound` | Nothing was observed between the start of the load and the next moment LoadLens got the game thread back. | Start-of-load to that moment - the load *plus* whatever else the frame did afterwards. Printed with a leading `<`: the load ended somewhere below that figure, not above it. |

An `upper bound` shows up for loads short enough that the flush never reported progress. The count is
exact in all three cases, which matters because the count is what the budget and the gate judge - the
milliseconds are there to tell you which row to look at first.

---

## Automated tests

Run *Window > Test Automation*, filter for `LoadLens`, or from the command line:

```
UnrealEditor-Cmd.exe MyProject.uproject -ExecCmds="Automation RunTests LoadLens" -unattended -nullrhi
```

Six tests, covering the places the plugin can be quietly wrong:

| Test | What it pins down |
| --- | --- |
| `LoadLens.Logic.RankRecordsSortsByTotalTimeAndIsStableOnTies` | Worst first, and a total order, so rows cannot swap places while somebody is reading them. |
| `LoadLens.Logic.EvaluateBudgetHitsTheBoundariesExactly` | Sitting on the budget is not a failure; the warning band's edges; the exit codes. |
| `LoadLens.Logic.IgnoredPathsDoNotCount` | Prefix matching, case-insensitivity, and that a blank row in the settings does not switch the plugin off. |
| `LoadLens.Logic.SamePathTwiceIsOneRowWithCountTwoAndAddedTime` | The counting rule, warm-up flags, imports being promoted to real loads, and the row cap. |
| `LoadLens.Logic.ShortenPathCutsTheMiddleAndKeepsTheAssetName` | The asset name survives whole, even when that makes the line too wide. |
| `LoadLens.Logic.SummarizeWorstNamesTheThingToFixAndIsNeverEmpty` | The bottom line is never blank, imports never win it, and an upper bound is printed as one. |

They are tagged `EditorContext | CommandletContext`, so they also run on a build server that has no
editor window open.

---

## Troubleshooting

**The box is green and I know something is loading.**
Check the second line. During the warm-up nothing counts against the budget - loads are still listed,
just not judged. Also check the ignore list: `/Engine/` and `/Script/` are ignored by default, and the
box reports how many loads that dropped.

**Nothing blocks in play-in-editor.**
The editor keeps everything it has ever loaded in memory, so a package it already touched cannot block
again, and the warm-up clock never starts because PIE does not fire `PostLoadMapWithWorld`. Use
*Play > Standalone Game* or a packaged build. This is an editor fact, not a plugin setting.

**`LoadLens.Provoke` says the asset is already in memory.**
It is, so loading it cannot block. Point the command at something cold, or restart and provoke before
anything else touches that asset.

**Every row says `upper bound`.**
That is a packaged non-editor build where the engine offers no end-of-load callback, and the loads were
short enough that the flush never reported progress. The counts are still exact. See
[What the millisecond figures are worth](#what-the-millisecond-figures-are-worth).

**The box does not appear at all.**
`bShowCounterBox` in the project settings, or `LoadLens.Show`. And check that something is actually
drawing it: either your GameMode's HUD class is `LoadLens HUD`, or your own HUD calls `DrawCounterBox`.

**Rows appear for packages my project never asked for.**
Those are imports, marked `import`, pulled in underneath a load that is in the list above them. They
carry no milliseconds of their own on purpose.

**The table says loads were counted but not named.**
The row cap was reached (512 packages by default). Raise **Max Tracked Packages**, or take the number as
the finding it is: a project doing several hundred distinct blocking loads has a bigger question to
answer than which one was worst.

**The gate exits 0 but I saw a hitch.**
The gate clears the ledger when it starts and only judges its own window, and it only judges *blocking
loads*. A hitch from level streaming, shader compilation or a GC spike is a different cause - LoadLens
will honestly report nothing, because nothing blocked.

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
