# DinoGame

Multiplayer dinosaur survival horror. **Unreal Engine 5.8.1.** Started from the UE first-person
template and converted to C++; multiplayer went in before any gameplay, deliberately, to avoid
retrofitting replication later.

Team process (version control, art, playtests, distribution) lives in `docs/PIPELINE.md`. Live
tasks and bugs live in Notion. This file is about working in the codebase.

---

## What tooling can and cannot edit

- **C++ under `Source/DinoGame/` is directly editable.**
- **`.uasset` and `.umap` are binary.** Blueprints, widgets, levels, and attenuation assets can
  only be changed by a human in the editor. Anything Blueprint-side has to be handed over as
  click-through steps.

This shapes the architecture: logic belongs in C++, with the Blueprint reduced to layout and
asset references. `UDinoJoinMenu` is the pattern — all behaviour in C++, `BindWidget` members
naming what the Blueprint must contain, so a renamed widget fails to compile rather than
silently doing nothing.

---

## Build and package

```
package.bat            build + cook + package
package.bat publish    ...and upload to itch
```

Single target, for a compile check:

```
"H:\Unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat" DinoGame Win64 Development -Project="H:\Unreal\Projects\DinoGame\DinoGame.uproject" -WaitMutex
```

`package.bat` hardcodes engine and project paths, so it only works on the original machine.
Parameterise it before a second programmer joins.

### Rules that are not obvious, and have each cost hours

**The editor target must be rebuilt before cooking.** Cooking runs through
`UnrealEditor-DinoGame.dll`. A stale editor module cooks Blueprints against an out-of-date C++
layout; the build *succeeds*, and the package then crashes on launch with
`ObjectSerializationError: ... Bad export index` before a window ever appears. `package.bat`
builds `DinoGameEditor` first for exactly this reason. Any change to a C++ class a Blueprint
inherits from — a new `UPROPERTY`, a new component — requires it.

**Live Coding only blocks the *editor* target.** The game target compiles fine with the editor
open. What actually blocks a game-target build is a running packaged `DinoGame.exe` holding
`Binaries/Win64/DinoGame.exe` open. Check for `DinoGame` processes, not the editor.

**A map reached only by travel is not packaged unless it is listed.** The cooker includes the
default map and what it references; a map you `ServerTravel` to by path is referenced by
nothing. List every playable map under `MapsToCook` in `Config/DefaultGame.ini`. Missing, the
package builds cleanly and then hosting logs `TickWorldTravel failed ... Failed to load` and
drops back to the default map — which looks like a broken menu, not a missing map.

**Config is baked into the `.pak`.** A packaged build contains no loose `.ini` files, so any
`Config/*.ini` change needs a full repackage. Copying just the exe ships new code with old
config, and fails silently.

| Change | Needs |
|---|---|
| C++ logic only, no layout change | Game target build; copy `DinoGame.exe` |
| New/changed `UPROPERTY`, component, class layout | Full package |
| Any `Config/*.ini` change | Full package |
| Blueprint or asset edits | Full package |

Builds are stamped with the git short SHA (`-dirty` when the tree is uncommitted) via
`Build/DinoBuildVersion.txt`, compiled in as `DINO_BUILD_VERSION`. The version shows on screen in
game and in `DinoNetStatus`. When two players disagree about behaviour, different builds is the
first thing to rule out.

---

## Multiplayer

Player-hosted **listen server** over **Steam Sockets / Steam Datagram Relay**. Peers are
addressed by SteamID rather than IP, so Steam handles NAT traversal and the host needs no port
forwarding. `UDinoSessionSubsystem` wraps `IOnlineSession` rather than Steam APIs, so the backend
can be swapped by changing `DefaultPlatformService`.

**Steam must be running and signed in** on every machine, with a different account per machine.
A missing Steam client presents as "0 sessions found"; `DinoNetStatus` reports `backend` and
`LAN fallback`, which is the fastest way to see it.

`SteamDevAppId=480` is Valve's shared Spacewar test ID, so a plain session search returns other
developers' lobbies. That is why joining is by **join code** rather than a server browser.

### Traps here

- **Seamless travel cannot promote a Standalone game to a listen server.** It reuses the existing
  network context and silently drops `?listen`, leaving the game standalone with no net driver.
  `ADinoGameMode` sets `bUseSeamlessTravel = true` for in-session map changes, so the *first*
  host transition clears that flag to force a hard travel. See `HandleCreateComplete`.
- **Steam lobby search is region-limited.** `AddRequestLobbyListDistanceFilter` is hardcoded in
  the engine with no config hook, so a distant tester may not find a lobby even with a correct
  code.

---

## Rounds

**Main menu → lobby → round → spectate → round over → restart.** The game boots into
`Lvl_MainMenu` (`GameDefaultMap`); the editor still opens `Lvl_FirstPerson`, so Play in the
editor goes straight to the lobby.

`ADinoGameMode` is an **`AGameMode`**, for its replicated match state machine: `WaitingToStart`
is the lobby (held there by `bDelayedStart` until the host starts), `InProgress` a round,
`WaitingPostMatch` round over. Restarting reloads the map with `ServerTravel` rather than
resetting actors by hand.

### Traps here

- **`AGameMode` needs an `AGameState`.** Pairing it with an `AGameStateBase` logs an error and
  the match state never reaches clients. `ADinoGameState` derives from `AGameState` for this.
- **AI sight registers every pawn**, spectator cameras included
  (`UAISense_Sight::bAutoRegisterAllPawnsAsSources`), and a spectator pawn has a player behind
  it. Targeting must check for a living `ADinoCharacter`, not "a pawn with a player" — the
  looser rule had dinos hunting the host's spectator camera.
- **`bIsAlive` means "has a living character this round"** and defaults to false. Any looser
  meaning counts lobby players and late joiners as alive, and the round can never end.
- **PlayerCanRestart is the only spawn gate**, and the engine also asks it for late joiners and
  for a dead client clicking. It returns true only inside `HandleMatchHasStarted`.
- **Seamless travel is off in PIE** (`net.AllowPIESeamlessTravel`), so an editor restart is a
  full reconnect. Auto-starting the next round therefore waits for a head count carried in the
  travel URL, not just `NumTravellingPlayers`.
- **Restarts are relative travels, which inherit the previous URL's options** — that is what
  keeps `?listen`. Any option meant to be one-shot has to be written explicitly every time,
  even as `=0`, or the last restart's value carries over.
- **After a seamless restart `PostLogin` is skipped**; per-player setup belongs in
  `HandleStartingNewPlayer`, which both paths reach.

The new screens (`UDinoSessionMenu`, `UDinoSpectatorOverlay`) are laid out in C++ with
`WidgetTree->ConstructWidget` — no Blueprint to keep in step. Spectator controls are input
actions created at runtime in `ADinoSpectatorPawn`, so no input assets either.

---

## Voice chat

Unreal's built-in VOIP over the same connection — no hosted voice service, so no new central
dependency. Open mic (voice-activated, no push-to-talk). `UVOIPTalker` on `ADinoCharacter`,
spatialised with distance falloff and occlusion through geometry.

`UVOIPTalker` is a `UActorComponent`, not a scene component: spatialisation comes from
`Settings.ComponentToAttachTo`, and occlusion applies only to spatialised sources.

Tuning lives in `Config/DefaultEngine.ini` under `[Voice]`, `[SystemSettings]`, and
`[/Script/Engine.AudioSettings]`. **All of it is read once at startup**, so changes need a
repackage, not a restart. `VoiPSampleRate` defaults to 16 kHz and sounds like a telephone;
`voice.JitterBufferDelay` trades latency against choppiness.

---

## Console commands

**The full list, and how to open the console, is `docs/CONSOLE.md`.** It is the only list —
not repeated here, so there is one copy to keep right.

**Update `docs/CONSOLE.md` in the same change as the code** whenever a `UFUNCTION(Exec)` on
`ADinoPlayerController` is added, removed, or renamed; its arguments or defaults change; it
becomes host-only or stops being; or a `TAutoConsoleVariable` is added or removed. A reference
that is wrong is worse than none — it is what gets trusted mid-playtest.

Check nothing is missing — silent when complete, names any command the file lacks:

```
for c in $(grep -A1 "UFUNCTION(Exec)" Source/DinoGame/DinoPlayerController.h | grep -o "Dino[A-Za-z]*(" | tr -d '('); do grep -q "\`$c" docs/CONSOLE.md || echo "missing: $c"; done
```

### Two exec gotchas

- **Exec functions must live on the PlayerController.** `UFUNCTION(Exec)` on a GameInstance
  subsystem compiles, links, ships in the binary, and appears in console autocomplete — but is
  never called, because the console routes through `ULocalPlayer` to the `PlayerController`.
  Autocomplete listing a command proves the `UFunction` exists, not that it is reachable.
- **Optional exec parameters need non-empty `CPP_Default_<name>` metadata.** `= 4` works; an
  empty-string default is indistinguishable from having no default and fails with
  `Bad or missing property`. Hence a parameterless `DinoHost` alongside `DinoHostMap`.

---

## Conventions

- **Anything a player is meant to read goes through `DinoScreenLog` / `DinoScreenError`**
  (`DinoGame.h`). They log *and* mirror on screen. A plain `UE_LOG` is invisible on any machine
  not launched with `-log`, which is every machine running the game from itch.
- **Comments explain why, not what** — particularly where engine behaviour is counter-intuitive
  and the code would otherwise look arbitrary.
- **Temporary code is marked and greppable**: `grep -rn "TODO(" Source/ Config/`.
  `TODO(join-ui)` is console scaffolding to delete once the menu ships; `TODO(ship)` is the
  placeholder Steam AppID.

---

## Where knowledge lives

| | |
|---|---|
| `CLAUDE.md` | How to work in this codebase (this file) |
| `docs/PIPELINE.md` | Team process: version control, art, builds, playtests |
| `docs/CONSOLE.md` | Every console command and how to open the console. Kept in step with the code |
| Notion, "Dino Game" | Live tasks and bugs. **"Dino Game (BACKUP)" is a duplicate — never write to it** |
| `Builds/Windows/DinoGame/Saved/Logs/DinoGame.log` | What a packaged run actually did |

Reading the log is usually faster than reasoning about what went wrong, and it is written whether
or not `-log` was passed.
