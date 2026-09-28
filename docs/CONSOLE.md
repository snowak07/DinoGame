# Console commands

## Opening the console

1. Click into the game viewport so it has keyboard focus.
2. Press **`~`** (the key under Esc). Once for a one-line bar, **twice** for the full console
   with scrollback — use the full one for anything that prints several lines.
3. Type a command. **Tab** autocompletes.

Works in PIE and Development builds. The console is compiled out of Shipping.

If `~` does nothing, the keyboard layout may not map it: check
**Project Settings → Input → Console Keys**.

**Host only** means run it on the machine hosting the session — AI exists only there. Solo PIE
counts as the host. With several PIE players, use the server's window.

---

## AI

| Command | Host only | |
|---|---|---|
| `DinoAICheck` | yes | PASS/FAIL setup check per creature: StateTree, EQS query, navmesh, eye height, attack component, vulnerability |
| `DinoAIStatus` | yes | One line per creature: awareness, target, live contact, last known location, attack phase |
| `DinoAIHistory` | yes | The last 12 search legs, newest first |
| `DinoAIDebug` | no | Toggle debug draw on this machine |
| `DinoSetState <state>` | yes | Force every creature into a state and lock it there |
| `DinoSetState auto` | yes | Release the lock; perception takes over again |
| `DinoHitDino [damage]` | yes | Hit the nearest living creature. Damage defaults to 25 |

`DinoSetState` takes partial names: `unaware`, `sus`, `alert`, `hunt`, `search`, `att`.
Use `att` rather than `a` — `a` matches **Alerted** first.

`DinoHitDino` stands in for player attacks until those exist. It picks the *nearest* living
creature, including one with its AI switched off, so stand closer to the one you mean.

### Console variables

| Variable | |
|---|---|
| `DinoAI.DebugDraw 1` | Debug draw on (`0` off). Same as `DinoAIDebug`, but explicit |

### Reading the debug draw

| Shape | Meaning |
|---|---|
| Capsule colour | Awareness: grey Unaware, yellow Suspicious, orange Alerted, red Hunting, blue Searching, **white Attacking** |
| Arrow | Which way the body faces |
| Cyan sphere + line | Where it sees from, and where it is looking |
| Coloured sphere + line | Where it is walking to right now |
| Small grey sphere | The next search point, already chosen |
| Magenta sphere + circle | Search centre and current search radius |
| Yellow sphere | Where it predicts you went |
| Orange cone (T-Rex) | Mouth zone — stand in it and you are eaten |
| Yellow line + sphere (raptor) | Where the lunge will land if it launched now |
| Red sphere (raptor) | Bite, during the lunge |
| Thick red line | Who is holding whom |

Destination and search markers draw on the host only; capsules, attack shapes and holds draw
everywhere.

---

## Multiplayer

| Command | |
|---|---|
| `DinoHost` | Host the current map with 4 slots and print the join code |
| `DinoHostMap <map> [slots]` | Host a specific map, e.g. `DinoHostMap Lvl_FirstPerson 4`. Slots default to 4 |
| `DinoCode` | Show your join code again |
| `DinoJoinCode <code>` | Join by code, e.g. `DinoJoinCode K7M2PQ`. Case and separators are ignored |
| `DinoFind` | List sessions found |
| `DinoJoin [index]` | Join a session from the `DinoFind` list. Index defaults to 0 |
| `DinoLeave` | Leave the session |
| `DinoMenu` | Open the host/join menu |
| `DinoNetStatus` | Build, backend, net mode, net driver, connections |
| `DinoVoiceStatus` | Voice config and who is talking |

In `DinoNetStatus`, `net driver : SteamSocketsNetDriver` means traffic is on Steam's relay.
`IpNetDriver` means it silently fell back to raw IP.

`DinoFind` and `DinoJoin` are scaffolding from before the join menu and will be removed.

---

## Other

| Command | |
|---|---|
| `DinoBuildVersion` | Show the build version on screen again. When two players disagree about behaviour, check this first |

---

*Every command here is a `UFUNCTION(Exec)` in `Source/DinoGame/DinoPlayerController.h`, and every
variable a `TAutoConsoleVariable` in `Source/`. This file is updated in the same change as any of
them — see "Console commands" in `CLAUDE.md`.*
