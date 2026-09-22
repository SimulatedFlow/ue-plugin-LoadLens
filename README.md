# LoadLens

**Catch the blocking loads that freeze your frame.**

LoadLens does not preload anything and does not fix anything. It tells you what blocked and what it
cost. That is the whole promise, and it is deliberately a small one.

Every synchronous asset load is caught the moment it happens - the package name, the milliseconds it
cost, the frame it happened in and, where it can be established honestly, the Blueprint function that
asked for it. On screen in a packaged build, and as a gate command that fails a build when the freezes
come back.

- **Documentation:** <https://wiki.teufel-engineering.com/en/LoadLens/documentation>
- **Support:** <mailto:teufelsilvan@gmail.com>

## What it is for

A `LoadSynchronous` in a Blueprint function that runs on a weapon switch costs 40 ms the first time and
nothing afterwards. On the machine it was written on, nobody notices. On a player's slow disk, that is a
visible stop, every time they equip that weapon for the first time in a session.

The engine already has the hook - `FCoreDelegates::OnSyncLoadPackage` reports the package name on every
blocking load. What it does not have is a list, a budget and a check that fails a build. LoadLens is
those three things.

## What it is not

LoadLens is not level streaming. A streaming cell coming in late is a different hitch with a different
cause, and most projects have both. That one is **StreamGuard**; the two are complementary, not
competitors.

It is also not Unreal Insights. Insights is a better recorder than this will ever be. It is also a second
program, a recording, and a thing nobody opens until the week before submission. LoadLens is a number on
the screen while somebody is playing, in the build they will ship.

## In one minute

1. Enable the plugin.
2. Set your GameMode's HUD class to `LoadLens HUD` - or call
   `ULoadLensSubsystem::DrawCounterBox(Canvas)` from your own `AHUD::DrawHUD`.
3. Play. Wait three seconds for the warm-up to close. The box is green.
4. Type `LoadLens.Provoke` in the console. It goes red, names the package and prints the milliseconds.

## The console

| Command | What it does |
| --- | --- |
| `LoadLens.Show` / `LoadLens.Hide` | Show or hide the counter box. |
| `LoadLens.Dump` | Write the whole table to the log, not just the five rows on screen. |
| `LoadLens.Reset` | Throw the ledger away and start counting again. |
| `LoadLens.Budget <n> [warn]` | Blocking loads allowed after the warm-up, and the warning limit. |
| `LoadLens.Report [path]` | Write the ledger as JSON. |
| `LoadLens.Gate <seconds> [-noexit]` | Measure, write the report, exit 0 / 1 / 2. |
| `LoadLens.Provoke [asset]` | Deliberately trigger one blocking load, to see the box react. |

## The gate

```
LoadLens.Gate 60
```

Measures for sixty seconds, writes `Saved/LoadLens/report.json`, and ends the process with:

- **0** - no blocking loads after the warm-up (or no more than the budget allows)
- **1** - over budget, still at or under the warning limit
- **2** - above the warning limit

The same three exit codes as LocaleGuard, AssetWarden and WidgetLedger.

## Requirements

Unreal Engine 5.8, Win64. One runtime module, no editor module, no UMG, no Slate. Everything survives a
packaged Shipping build.

## Licence

Copyright 2026 Silvan Teufel. All Rights Reserved.
