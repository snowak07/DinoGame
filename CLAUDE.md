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
package.bat deploy     ...and copy to the test PC over the LAN (deploy.bat)
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

## PIE and packaged builds must behave the same

Development happens in Play In Editor (PIE); playtests run the packaged build from itch, over
Steam, on separate machines. Several playtest bugs only existed in the packaged build. Known
differences:

| | PIE | Packaged |
|---|---|---|
| Round restart | Full reconnect (seamless travel is off in PIE) | Seamless travel |
| Steam, lobbies, join codes | Not used | Used |
| Voice chat | Effectively untested | Live, between real machines |
| Network timing | One process; replication arrives almost instantly and in order | Real latency; things arrive late and out of order |
| Start | `Lvl_FirstPerson` lobby | Main menu, then host or join |
| Config | Read live | Baked in at package time |

**Rule:** any change touching travel, sessions, voice, spawning or possession, death, spectating,
or what replicates when must state how it behaves in *both*. Prefer code that does not depend on
which one it is, for example waiting for data to arrive instead of assuming it already has.
Where something can only be proven packaged, say so plainly and test it on two machines before
the next playtest, rather than calling it done because PIE worked. Turning on
`net.AllowPIESeamlessTravel 1` brings PIE restarts closer to packaged ones.

---

## Multiplayer

Player-hosted **listen server** over **Steam Sockets / Steam Datagram Relay**. Peers are
addressed by SteamID rather than IP, so Steam handles NAT traversal and the host needs no port
forwarding. `UDinoSessionSubsystem` wraps `IOnlineSession` rather than Steam APIs, so the backend
can be swapped by changing `DefaultPlatformService`.

**Steam must be running and signed in** on every machine *before the game starts*, with a
different account per machine. Without it the engine silently falls back to the Null subsystem
(local network only), and Steam cannot be picked up later without a restart. In a packaged build
`UDinoSessionSubsystem::IsOnlineBackendMissing` catches this: the main menu says so and Host and
Join stay disabled, as do the `DinoHost` / `DinoJoinCode` / `DinoFind` commands. The editor is
exempt, because PIE never loads Steam. `DinoNetStatus` shows `backend`, `LAN fallback`, and a
red `PROBLEM` line in this state. The log line to look for is
`SteamAPI failed to initialize: Cannot create IPC pipe to Steam client process`.

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
- **"Alive" means "has a living character this round"** (player status Alive or Downed), and a
  player starts as Lobby. Any looser meaning counts lobby players and late joiners as alive, and
  the round can never end.
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
- **Seamless travel carries the world clock over** (`LoadedWorld->TimeSeconds` is copied from the
  old world). Never time "since this map started" by comparing `GetTimeSeconds()` to a constant:
  minutes into a session it is already past. Record a start time and compare to that.
- **Destroying a spectator's old body can destroy their spectator camera.** The engine's
  `AController::PawnPendingDestroy` sends the controller to Inactive, which removes the camera,
  and on a client the destruction can arrive while the controller still holds the body. That
  froze joining players' views on their corpse. Dead bodies now stay until the map reloads, and
  `ADinoPlayerController::PawnPendingDestroy` keeps a spectator spectating regardless.
- **A seamless restart keeps each PlayerController, including its input mode**, while removing
  its widgets. Anything that set UI-only input must be undone explicitly on round start, not
  only when its widget is found still on screen.
- **`ReadyToStartMatch` must not say yes before the world has begun play.** AGameMode asks it
  from `StartPlay`, and a yes there skips `NotifyBeginPlay` and starts the round first —
  characters are possessed before their world begins play, and the player cannot move or look.
  Solo restarts hit this every time, because everyone is already present when the map starts.

### Player status

Each `ADinoPlayerState` has a status (Lobby, Alive, Downed, Dead) and, when Dead, a cause. They
replicate together as one struct. It is the single answer to "where is this player in the round";
spectating, the round end, the Tab menu and, next, voice all read it.

- **Only the server changes it** (`SetStatus`), and every change is pushed at once with
  `ForceNetUpdate`. A PlayerState otherwise sends updates once a second.
- **Every death goes through `ADinoGameMode::KillPlayer(Controller, DamageType)`.** Lethal
  damage reaches it from the character; falling below Kill Z reaches it from
  `ADinoCharacter::FellOutOfWorld`, which replaces Unreal's default of silently deleting the
  character. A new way to die calls `KillPlayer`, not `SetStatus`.
- **The cause is the damage type class** (`UDinoDamageType_Bite`, `_Devour`, `_Fall`), carried
  through `ApplyDamage` as Unreal intends. Each attack component reports its own through
  `GetDamageType`.
- **Listen on `ADinoGameState::OnPlayerStatusChanged` and `OnPlayerListChanged`**, and rebuild
  from everyone's current status in the handler rather than applying the one change. A late
  joiner or a late binder never sees the changes before it. On a client a PlayerState can also
  arrive before the game state, and its relayed change is then skipped.
- **Server reactions vs local reactions.** Round end, spectating and voice muting are decided on
  the server. UI, camera and listening position are local. The event fires on every machine, so
  check which side you are on.
- **Anything that must happen in order stays a step inside `KillPlayer`**, not a separate
  listener. Unreal does not guarantee listener order.

The new screens (`UDinoSessionMenu`, `UDinoSpectatorOverlay`) are laid out in C++ with
`WidgetTree->ConstructWidget` — no Blueprint to keep in step. Spectator controls are input
actions created at runtime in `ADinoSpectatorPawn`, so no input assets either.

---

## Voice chat

Unreal's built-in VOIP over the same connection — no hosted voice service, so no new central
dependency. Open mic (voice-activated, no push-to-talk). Spatialised with distance falloff and
occlusion through geometry.

**Each player's voice is a `UDinoVoiceTalker` on their `ADinoPlayerState`, not on the
character.** A talker on the character only existed while a body did, so voices with no body
yet (the lobby, the first moments of a round) or any more (the dead) played unpositioned, at
full volume to everyone. The PlayerState lasts the whole session; the character only lends it a
position (`SetVoiceBody`), and `RefreshVoicePlacement` decides from scratch where the voice
comes from whenever status or body changes. Alive with a body: from the body, with attenuation.
Anything else: unpositioned.

- `UVOIPTalker` is a `UActorComponent`, not a scene component: spatialisation comes from
  `Settings.ComponentToAttachTo`, and occlusion applies only to spatialised sources.
- **The engine reads those settings only when a speaker's voice starts playing**
  (`FVoiceEngineImpl::SubmitRemoteVoiceData`), and keeps the sound player going between
  sentences. Changing the settings does nothing to a voice already playing, so
  `UDinoVoiceTalker::RefreshVoice` releases the player and the next packet starts a new one.
- **The engine's talker lookup holds one talker per net ID, and a talker leaving deletes
  whatever entry has its ID**, even one a newer talker took over. Every seamless restart gives
  each player a new PlayerState on the server, registers its talker, *then* destroys the old one
  (`AGameMode::HandleSeamlessTravelPlayer`), so from the first restart the host heard every
  client unpositioned. `UDinoVoiceTalker::UnregisterKeepingNewer` puts the newer entry back.
  Register in `OnSetUniqueId`, not `OnRep_UniqueId`: on the server the ID is set at login, after
  BeginPlay, and RepNotifies never fire there. `DinoVoiceStatus` flags `NOT REGISTERED`.
- The engine's talker lookup (`UVOIPStatics`) is one map for the whole process, so in
  multi-client Play In Editor the clients overwrite each other's talkers. Proximity and placement
  can only be tested packaged, on two machines.

**Who hears whom is the server's call, rebuilt from player status** (`ADinoGameMode::
RefreshVoiceRules`, on every status, player list, match state and spectator camera change). It
uses the engine's gameplay mute, so the server stops forwarding the voice rather than each client
discarding it. During a round the living never hear the dead; the dead hear the dead, and the
living only in the follow and free spectator cameras, not overhead. Following, the listening
position moves to the teammate (`ADinoSpectatorPawn::UpdateHearing`); free, it is the camera. Lobby and round over: everyone hears
everyone. A Tab-menu mute is the engine's player mute, a separate reason, so neither undoes the
other.

**The listen-server host needs its own unmute** (`ADinoGameMode::UnmuteForGameplay`). The host's
controller is both the server's and the client's copy of itself, sharing one mute list, so the
gameplay mute's client half adds a second reason to the same list and the gameplay unmute never
completes. Without it the host stops hearing anyone who has ever died, for the rest of the session.

**Mic recovery is `UDinoVoiceSubsystem`.** The engine's Windows capture opens one mic, once: an
unplugged mic never came back and a changed Windows default was ignored. The subsystem watches
Windows device changes and reopens the right mic through the engine's own capture
(`IVoiceCapture::ChangeDevice`). It can only open a mic that existed at startup, because the
engine reads the device list once; a later one gets a "restart to use it" notice. Using any mic
without a restart is the Notion backlog item "Mic capture on AudioCaptureCore". Reaching the
capture needs the engine's protected members (see `GetEngineMicCapture`), written against 5.8.1:
check it on an engine upgrade.

**Mic sensitivity sets `voice.SilenceDetectionThreshold` at code priority.** Both it and
`voice.MicNoiseGateThreshold` are set in `[SystemSettings]`, which outranks the game-setting
priority `UVOIPStatics::SetMicThreshold` uses, so that call is silently ignored. Words cut off, or
crackle, from a quiet mic means the threshold is too high for it.

Tuning lives in `Config/DefaultEngine.ini` under `[Voice]`, `[SystemSettings]`, and
`[/Script/Engine.AudioSettings]`. **All of it is read once at startup**, so changes need a
repackage, not a restart. `VoiPSampleRate` defaults to 16 kHz and sounds like a telephone;
`voice.JitterBufferDelay` trades latency against choppiness.

**Seamless travel crashes voice chat unless `UDinoVoiceCleanupSubsystem` runs.** The engine gives
each remote speaker an audio component registered straight into the world, owned by no actor,
and only clears it after the *next* map loads (`FVoiceEngineImpl::OnPostLoadMap`). Seamless
travel destroys the old world first, so a restart while someone talks crashed the client on the
audio thread. The subsystem destroys those components on `OnWorldCleanup`, and again on shutdown.
Do not remove it. Stopping a voice must flush the audio thread before destroying its component
(`ReleaseSpeakerAudio`), or the engine's `OwningSynthComponent.IsValid()` ensure fires and the
game stutters.

**Push-to-talk must stay off in config** (`[/Script/Engine.GameSession] bRequiresPushToTalk=False`
in `DefaultGame.ini`). The engine default is on, and the server re-applies it to every player each
time it sets them up (`AGameModeBase::GenericPlayerInitialization`), which includes every seamless
restart. With it on, every mic switched off at each restart and stayed off.

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
