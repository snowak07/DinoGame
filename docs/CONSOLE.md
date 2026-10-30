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
| `DinoDown` | yes | Put yourself down, as a raptor's bite would. Needs a second player still standing: alone, nobody is left to revive you, so you bleed out at once and the round ends |
| `DinoRevive` | yes | Stand yourself back up from down, as a teammate's revive would |
| `DinoVisionTest [on\|off]` | yes | Freeze every creature and show, moment to moment, whether it can see you. No argument toggles |
| `DinoNoise [loudness]` | no | Make a noise where you stand, as if you spoke at that loudness: 0 whisper to 1 shout, default 0.5. Creatures in earshot come to investigate. For testing without a mic (Play In Editor has none) |

`DinoSetState` takes partial names: `unaware`, `sus`, `alert`, `hunt`, `search`, `att`.
Use `att` rather than `a` — `a` matches **Alerted** first.

`DinoHitDino` hits without aiming, the quick way to probe a creature's stagger threshold or
health; the flare gun (LMB) is the real weapon and goes through the same damage path. It picks
the *nearest* living creature, including one with its AI switched off, so stand closer to the one
you mean.

### Vision test

`DinoVisionTest` answers one question: *from where it stands, facing the way it faces, can this
creature see me right now?* Creatures stop moving, turning and attacking, and keep no memory:
no 3-second hunt linger, no search, no eyes left following you, no wider "lose sight" range
for someone already seen. Capsule goes red the instant you are in sight and grey the instant
you are not. `DinoVisionTest off` resumes normal AI.

For fixed test positions, place creatures in the level, rotate them to face where you want,
and tick **Vision Test On Start** (Details → Dino | AI | Debug) on each one. They start frozen.

| Shape | Meaning |
|---|---|
| Yellow fan | Sight range and field of view, at eye height |
| Green line to you | Sees you, with the distance and angle |
| Red line to you | Cannot, with why: too far, outside the cone, or blocked by *which actor* |

Draws on the host whether or not debug draw is on.

### Console variables

| Variable | |
|---|---|
| `DinoAI.DebugDraw 1` | Debug draw on (`0` off). Same as `DinoAIDebug`, but explicit |
| `dino.Noise.WhisperRange 500` | How far (cm) the quietest voice carries to a creature. Host |
| `dino.Noise.ShoutRange 2500` | How far (cm) a shout carries. Host |
| `dino.Noise.WallFactor 0.5` | How much of a voice's range is left through a wall. Host |
| `dino.Noise.ShoutMicLevel 0.3` | The mic level that counts as a shout (`DinoVoiceStatus` shows yours). Set on the *speaker's* machine |
| `dino.Noise.FootstepRange 600` | How far (cm) walking at full speed carries. Slower is quieter; crouch-walking is silent. Host |
| `dino.Noise.LandingRange 1000` | How far (cm) landing from a jump or a fall carries. Host |
| `dino.Stealth.CrouchSightFactor 0.6` | A crouched player is seen only within this fraction of a creature's sight range. Host |
| `dino.Stealth.CrouchCloseSenseFactor 0.5` | A crouched player is felt only within this fraction of the 3 m close sense. Host |
| `dino.Downed.BleedOutSeconds 45` | How long a downed player lasts untouched. Damage while down shortens it. Host |
| `dino.Downed.ReviveSeconds 4` | How long the revive key has to be held. Host |
| `dino.Downed.ReviveRange 200` | How close (cm) a reviver has to stay. Host |
| `dino.Downed.ReviveHealth 0.2` | Share of max health a revived player stands up with. Host |
| `dino.AI.FlaredVictimIgnoreSeconds 10` | How long a raptor shot off its victim leaves that victim alone. Host |

| `dino.Noise.Debug 1` | Draw how far voices carry (also on with `DinoAI.DebugDraw`). See below |

The `dino.Noise.*` values are read live, so they can be tuned mid-session from the console.

**Reading the noise draw.** It appears when someone talks or runs `DinoNoise`, and lasts 4.5 s.
While someone keeps talking, a new drawing appears only if it is louder than the one already
showing, or once that one has expired, so what is on screen is their loudest recent voice. The
rings lie on the floor under the speaker; the lines to creatures start at the mouth, where
hearing is measured from. The host sees everything; a client sees only the rings around its
own voice.

| Shape | Meaning |
|---|---|
| Yellow ring (on the floor) | How far this voice carries in the open |
| Orange ring (on the floor) | How far it carries through something |
| Green line to a creature | Clear line, and it heard you |
| Red line | Clear line, but too far |
| Orange line ending in a sphere | Blocked there, by the named actor. Grey from there on to the creature's eyes |
| Text over a creature | `HEARD` or `too far`, its distance / the range that applied |

The log line `Noise: ... heard by [...]` names the blocker for every muffled creature, so a
session can be reviewed afterwards without the draw.

### How creatures find you

| | |
|---|---|
| **Sight** | A 60° cone (30° each side), 40 m; **24 m if you are crouched**. Seeing you is what starts a hunt |
| **Up close** | Within 3 m of its body (**1.5 m crouched**), in any direction but not through walls, it notices you and comes to look |
| **Your footsteps** | Walking carries up to ~6 m, less when moving slowly. **Crouch-walking is silent.** Landing from a jump carries ~10 m |
| **Your voice** | While you talk, creatures in earshot come to look where you were. A whisper carries ~5 m, a shout ~25 m, half that through walls. Only the living make noise |
| **A flare hit** | It staggers, then goes to where the shot came from. For 3 s it can only lock on to the shooter, so shooting a raptor off a teammate draws it to you |

"Comes to look" is Searching, not Hunting: it knows where to look, not where you are.

While hunting, a creature goes after whoever it can see who is closest, and after switching sticks
with the new target for 2 s. A sound never pulls it off the player it is chasing, and nothing
does mid-attack. But if the player it is chasing is out of sight and talks, it hears where they are.

### Reading the debug draw

| Shape | Meaning |
|---|---|
| Capsule colour | Awareness: grey Unaware, yellow Suspicious, orange Alerted, red Hunting, blue Searching, **white Attacking** |
| Arrow | Which way the body faces |
| Cyan sphere + line | Where it sees from, and where it is looking |
| Coloured sphere + line (searching) | Where it is walking to right now |
| Small grey sphere (searching) | The next search point, already chosen |
| Thick red line (hunting) | To the player it is chasing, while it can see them |
| Red sphere + thin line (hunting) | Where it last saw them, while the hunt holds on without sight |
| Magenta sphere + circle | Search centre and current search radius |
| Yellow sphere | Where it predicts you went |
| Grey sphere + circle (idle) | Home, and how far it wanders from it |
| Grey sphere + line (idle) | Where it is strolling to |
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
| `DinoHost` | Host the gameplay map (`Lvl_FirstPerson`) with 4 slots and print the join code |
| `DinoHostMap <map> [slots]` | Host a specific map, e.g. `DinoHostMap Lvl_FirstPerson 4`. Slots default to 4 |
| `DinoCode` | Show your join code again |
| `DinoJoinCode <code>` | Join by code, e.g. `DinoJoinCode K7M2PQ`. Case and separators are ignored |
| `DinoFind` | List sessions found |
| `DinoJoin [index]` | Join a session from the `DinoFind` list. Index defaults to 0 |
| `DinoLeave` | Leave the session and return to the main menu |
| `DinoMenu` | Same as Tab: the session menu during a match, the host/join menu anywhere else |
| `DinoNetStatus` | Build, backend, net mode, net driver, connections |
| `DinoVoiceStatus` | Your mic (device, level, capture state) and every other player's voice: talking, muted by you, heard from their body or unpositioned |
| `DinoMicRefresh` | Re-read the mic list and reopen the right mic. Same as **Reconnect mic** in the Tab menu |

In `DinoNetStatus`, `net driver : SteamSocketsNetDriver` means traffic is on Steam's relay.
`IpNetDriver` means it silently fell back to raw IP.

If Steam wasn't running when the game started, a packaged build refuses `DinoHost`, `DinoHostMap`,
`DinoJoinCode`, `DinoFind` and `DinoJoin`, says so on screen, and `DinoNetStatus` shows a red
`PROBLEM` line. Start Steam, then restart the game. Play In Editor is unaffected.

`DinoFind` and `DinoJoin` are scaffolding from before the join menu and will be removed.

### Voice

`DinoVoiceStatus` reports the mic level as the loudest of the **last 3 seconds**, against the level
speech must reach to be sent. Talk, then run it: typing the command means you are not talking.
The full engine dump goes to the log only.

The **Tab menu**'s right-hand column has the same controls, all local to your machine:

| | |
|---|---|
| **Mute / volume** per player | Mute stops the server sending you their voice. Volume is 0-200%. Both last for the session |
| **Microphone** | *Windows default* follows the Windows default input, including changes mid-game. Picking a mic sticks to it while it is plugged in. Saved |
| **Mic sensitivity** | Raise it if your words are cut off or nobody hears you. Saved |
| **Reconnect mic** | Reopens the mic now |

A mic unplugged and plugged back in reconnects by itself. **Only mics connected when the game
started can be used**: one connected later gets a "restart to use it" notice.

---

## Rounds

| Command | Host only | |
|---|---|---|
| `DinoStartRound` | yes | Start the round from the lobby. Same as the Start Round button |
| `DinoRestartRound` | yes | Reload the map straight into a new round |
| `DinoLobby` | yes | Reload the map into the lobby |

The same three are buttons on the **Tab menu**, shown to the host only.

Playing in the editor starts in the lobby, so `DinoStartRound` (or the button) is the first
thing to do in any solo test.

### Player controls

| Input | |
|---|---|
| **LMB** (right trigger) | Fire a flare, at most once every 1.5 s. Knocks a raptor off a pinned player |
| **Left Ctrl** (hold) / **C** (toggle, right stick click) | Crouch: lowers your view, silences your footsteps, and makes you harder to see and feel |
| **E** (hold; gamepad X / left face button) | Revive a downed teammate in front of you, within 2 m. Keep holding for 4 s |

**Going down.** A raptor's bite takes your health to zero and puts you **down**, not dead: you
lie on your back as when pinned, unable to move or shoot, with your view held to a little room
either side (you can still talk), and a red bar shows how long you have (45 s, less while a
raptor keeps biting). You stay down if the raptor is shot off you. A teammate holding E next to you stands you back up at 20% health. A
T-Rex's bite and falls kill outright. If nobody is left standing, everyone still down bleeds out
and the round ends. Shooting a raptor off a downed player makes it leave them alone for 10 s.
| **Tab** | The session menu |

### Spectator controls

Dead players, lobby players and late joiners watch through the spectator camera.

| Input | |
|---|---|
| **Space** | Cycle camera: Follow a teammate → Free fly → Overhead |
| **LMB / RMB** | Next / previous living teammate |
| **Mouse** | Orbit the teammate (Follow), or look (Free) |
| **WASD, Q / E** | Fly, and down / up (Free) |
| **Tab** | The session menu |

The overhead view uses an actor tagged `DinoOverhead` if the level has one (normally a placed
Camera Actor), and otherwise frames everyone from high above.

What a spectator hears depends on the camera. **Follow**: the living, as the followed teammate
hears them, plus the dead. **Free**: the living by distance from the camera, plus the dead.
**Overhead**: only the dead and other spectators. The
living never hear the dead during a round.

---

## Other

| Command | |
|---|---|
| `DinoBuildVersion` | Show the build version on screen again. When two players disagree about behaviour, check this first |
| `DinoSelfCheck` | Check this machine's voice, spectating and round state against the game's rules, and on the host also every creature's target and every pin. Failures on screen, every result in the log |

`DinoSelfCheck` also runs by itself 5 seconds after every phase change, death and join, on every
machine. If it finds a problem it says so in red: run `DinoSelfCheck` then to see what, and keep
the log. The host checks more than a client (what the server sends to each player, and everyone's
status). Some checks are skipped in Play In Editor, where they can't give a true answer.

---

*Every command here is a `UFUNCTION(Exec)` in `Source/DinoGame/DinoPlayerController.h`, and every
variable a `TAutoConsoleVariable` in `Source/`. This file is updated in the same change as any of
them — see "Console commands" in `CLAUDE.md`.*
