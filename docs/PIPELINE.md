# Dino Game — Pipeline Plan

How the team's code, art, builds, and playtests are organized. This lives in the repo at
`docs/PIPELINE.md` so it's versioned alongside what it describes.

Fill in every `TODO` before handing this to anyone.

- **Engine version:** 5.8.1 — installed at `H:\Unreal\UE_5.8`. Everyone runs this build.
- **Repo:** TODO — `https://github.com/<owner>/<repo>` (private)
- **Backup remote:** TODO — Gitea on the TrueNAS server
- **Art share:** TODO — `\\truenas\<share>`
- **itch project:** TODO — `<user>/dino-game`
- **Task tracking:** Notion — Programming Tasks, Bugs & QA, Mechanics & Systems

---

## 1. Decisions, and why

| Area | Choice | Reason |
|---|---|---|
| Version control | GitHub, private repo, Git + LFS | Unlimited free collaborators — the only free host that doesn't charge per seat at 7+ people. LFS locking supported. |
| In-editor UI | Project Borealis UEGitPlugin | Artists and level designers use Unreal's Revision Control menu. They never touch Git. |
| Source files (PSD, ZBrush, Blender) | Shared Google Drive folder, archived to the NAS | 10–50× larger than what they export to and never loaded by the engine. An archive, not a working store. See §3. |
| Backup | Gitea on TrueNAS, second remote | Full independent copy. Costs nothing; you already own the hardware. |
| Build distribution | itch.io + butler | Delta patching and auto-update in the itch app. Playtesters need no Unreal, no Git, no repo. |
| Multiplayer connectivity | **Steam Sockets / SDR, implemented** | Peers are addressed by SteamID, not IP. Steam handles NAT traversal and relay fallback, so hosts need no port forwarding and no VPN. See §7. |
| Voice chat | **Unreal built-in VOIP, proximity, implemented** | Rides the existing game connection. No hosted voice service, so no new central dependency or per-user cost. See §7. |

**Rejected, for the record:** Azure DevOps and Diversion (free tiers stop at 5 users);
Anchorpoint (free tier is single-user with no file locking); self-hosting the primary
repo (uptime and bandwidth land on one person); GitHub + S3 LFS proxy (the proxy
implements transfers only, not the locking API).

**Dropped after Steam Sockets landed:** Tailscale and Epic Online Services. Both existed
solely to solve NAT traversal for a direct-IP connect flow that no longer exists. Keeping
either would mean a VPN install for seven people, or a day of EOS setup, to buy something
already working. Revisit EOS only if the Steam dependency itself becomes a problem.

---

## 2. Version control

### What goes in the repo

Engine-ready assets and code only: `.uasset`, `.umap`, C++, config, this doc.

**Never committed** — these regenerate locally and are enormous:

```
Binaries/  Intermediate/  Saved/  DerivedDataCache/  .vs/  *.sln
```

### Locking

`.uasset` and `.umap` are binary and cannot be merged. There is no merge strategy for
them; the only workable model is exclusive locks. They're marked `lockable` in
`.gitattributes`, so Git LFS refuses concurrent edits.

The workflow, as artists experience it in Unreal:

1. Right-click asset → **Check Out** (takes the lock)
2. Edit
3. Right-click → **Submit to Revision Control** (commits, pushes, releases the lock)

Lock state shows on asset thumbnails: red check = yours, blue check = someone else's.

### Setup notes

- UEGitPlugin ships as source and must be compiled with Visual Studio (C++ workloads).
- `bSCCAutoAddNewFiles=False` is required by the plugin and is intentional.
- `.gitattributes` needs explicit per-file entries rather than broad wildcards — follow
  the plugin's README and use its reference `.gitignore` / `.gitattributes`.
- **One File Per Actor** is enabled in World Partition. Each actor becomes its own file,
  so two people can work in the same level as long as they don't touch the same actor.
  Without it, a level is one giant lockable binary.

---

## 3. Where art lives

**Art lives in GitHub.** Meshes, textures, materials, animations — once imported, they're
`.uasset` files, and every one of them is in the repo, locked and version controlled like
any other asset. This is the copy the game is built from and the copy everyone needs.

What's *not* in the repo is the **source files** that produce those assets: `.psd`,
`.ztl`, `.blend`, high-poly sculpts, raw scans, reference boards. Unreal never opens
them. They're typically 10–50× the size of what they export to, and LFS history can't be
pruned server-side, so committing them means paying for them permanently with no
runtime benefit.

### Source files are an archive, not a working store

This distinction is what keeps the setup from getting cumbersome.

Artists already work on their own machines, on their own drives — that doesn't change,
and nothing should require a VPN to open a PSD. What a shared location is *for* is
narrow:

- Recovering a source file if someone's drive dies
- Finding the source when an asset needs revising and its original author is unavailable

It is written to occasionally and read from rarely. It does not need low latency,
locking, or daily access — so the friction of reaching it barely matters.

### Decision: a shared Google Drive folder

One folder, shared with the team, mirroring the repo's asset folder names so any
`.uasset` can be traced back to the file that produced it.

**Storage is charged to whoever uploads, not to whoever owns the folder.** On personal
Google accounts this is the "owner rule": files added to a shared folder count against
the uploader's own quota, alongside their Gmail and Photos.

> **Buying a Google One plan does not, by itself, give the team space.** It raises Sam's
> quota only. Artists uploading to the shared folder still spend their own 15 GB. Paying
> without doing one of the two things below solves nothing.

### Decision: Sam owns every file in the store

Sam holds a paid Google One plan and every file in the archive is owned by his account,
so it draws on his quota alone. **Nobody else needs a plan, a family group, or any
setup — and no artist ever has to think about space.**

Start at **100 GB ($1.99/mo)** and move to 200 GB ($2.99/mo) when it gets close.
Upgrades apply immediately and prorate, so there's no reason to over-buy up front. There
is nothing between 200 GB and 2 TB ($9.99/mo).

### How files get in

Ownership follows whoever *creates* the file in Drive — and **a copy is a new file owned
by whoever made the copy.** That's the mechanism:

1. Artist shares the source file from their own Drive (or sends it any other way —
   WeTransfer, a USB drive at a meetup; the route doesn't matter).
2. Sam **makes a copy** into the archive folder. The copy is his, on his quota.
3. Artist deletes their original and empties their trash, reclaiming their own 15 GB.

Direction matters here. If Sam shares a folder *to* the team and an artist adds a file to
it, that file is the artist's and uses the artist's quota — the exact thing this is meant
to avoid. Files always land by Sam copying them in.

Do this as a monthly batch, together with the NAS archive pull below. The files pass
through Sam's hands anyway, so it's one session rather than two.

*Rejected:* Google One family sharing does pool storage, but it caps at 6 people and
anyone already in a family group would have to leave one and wait 12 months. Google
Workspace Shared Drives pool properly but cost roughly $7–8/user/month. Nextcloud on the
TrueNAS would be free and unlimited, but it's a project and needs a Cloudflare Tunnel or
Tailscale for remote access. Revisit only if the monthly batch becomes a chore.

### Archiving to the NAS

Periodically pull the Drive folder down to a TrueNAS dataset with ZFS snapshots. Do this
because you want a durable archive that isn't dependent on six other people's Google
accounts — **not** as a way to stay under the free tier.

Offload-to-free-space technically works, with two gotchas:

- **Trash holds quota for 30 days.** Deleting doesn't free anything until the trash is
  emptied.
- **You can only free space for files you own.** Deleting an artist's upload doesn't
  reclaim their quota; only they can do that.

But it's a manual chore, and manual chores get skipped for three months and then become a
two-hour job. At $1.99/mo, paying costs less than the time of a single offload session.
Archive to the NAS for durability; don't build a routine around dodging two dollars.

---

## 4. The backup

GitHub is the working remote and the only one the team ever touches. The TrueNAS holds a
full second copy, pulled automatically on a schedule.

**This is Sam's job alone.** Teammates never push to a backup remote — their entire
surface is Check Out and Submit inside Unreal. Adding a second push target to their
workflow would double their submit time and invent a new failure mode (backup
unreachable → submit looks broken → someone stops submitting). A scheduled mirror is
strictly better: consistent, requires nobody to remember anything, and when it fails
only one person needs to care.

### Setup: Gitea pull mirror

Gitea installs from the TrueNAS SCALE app catalog as a container — no changes to the
host OS, which matters because recent SCALE versions ship an immutable root filesystem
where `apt` is disabled.

1. Install the **Gitea** app, backed by its own dataset.
2. In Gitea: **New Migration → GitHub**.
3. Enter the repo URL and a GitHub **fine-grained PAT** with *Contents: read-only*.
   Never the account password, and never a token with write scope — a backup only reads.
4. Check **This repository will be a mirror**.
5. Set the sync interval to **daily**, or every few hours at most. See the bandwidth
   note below.
6. Add a ZFS periodic snapshot task on the Gitea dataset. That's what turns a mirror
   into real history — the mirror tracks GitHub's current state, snapshots let you go
   back to last Tuesday.

Gitea's pull mirrors sync LFS objects, not just pointers ([merged in 2021](https://github.com/go-gitea/gitea/pull/14726)).
This isn't covered in Gitea's mirror docs, so verify it once rather than assuming.

> **Set it up as a mirror at creation time.** Gitea can't convert an existing repository
> into a pull mirror afterward. Getting this wrong means deleting and redoing it.

### Bandwidth warning

Every mirror sync that pulls new LFS objects counts against your GitHub LFS **bandwidth**
allowance — the same 10 GiB/month the team's clones draw from. A mirror set to sync every
10 minutes can quietly become your largest bandwidth consumer. Daily is plenty for a
backup.

### Verify it, once

A backup you've never restored is a guess.

```bash
git clone <gitea-url> /tmp/restore-test
cd /tmp/restore-test
git lfs pull
```

Open a large `.uasset` and check its size. A few hundred bytes means it's still a pointer
and the objects never came across. Real size means the backup works. Delete the test
clone afterward, and re-run this any time you change the mirror configuration.

### If you ever push to a second remote manually

Not the plan, but worth recording, because the failure is silent:

```bash
git remote add backup <url>
git config remote.backup.lfsurl <url>.git/info/lfs   # required
git push backup --all && git push backup --tags
git lfs push backup --all                            # separate step
```

`git push` moves commits and pointer files; it does **not** move LFS objects to a second
remote. And with more than one remote and no explicit `lfsurl`, git-lfs falls back to
`origin` — so the objects can end up back at GitHub while the backup fills with pointers
referencing bytes it doesn't have.

---

## 5. The LFS meter

LFS history is permanent — objects cannot be pruned server-side — so repo growth only
goes one direction and it is metered.

**GitHub Free/Pro includes 10 GiB storage and 10 GiB bandwidth per month.** Beyond that,
roughly $0.07/GB storage and $0.0875/GB bandwidth. Check current rates on your own
billing page; the metered rollout has been staged.

Two facts worth internalizing:

- **Uploads count toward storage only.** Pushing assets never consumes bandwidth.
  Artists committing all day is the cheap direction.
- **Bandwidth is downloads only** — clones and pulls. Onboarding is the expensive event.

At the project's current ~2 GB, storage is comfortably inside the free allowance. Seven
people cloning 2 GB is ~12 GB of bandwidth, marginally over the monthly allowance, so
avoid onboarding everyone in the same calendar month if you'd rather not pay cents.

### Budget and alerts

With a payment method on file, set an explicit budget so growth can't surprise you:

1. Go to **https://github.com/settings/billing**
2. Find **Budgets and alerts**
3. Create a budget scoped to the **Git LFS** product
4. Set a monthly amount you'd be comfortable seeing on a card — $10 is a sane start

Alerts fire by email and in the GitHub UI at **75%, 90%, and 100%** of the amount. For
metered products a budget can also stop usage at 100% rather than only warning —
confirm which behavior applies to LFS on your account, because "stops" and "warns" are
very different outcomes mid-playtest.

> Without a payment method, GitHub blocks LFS usage at the free quota instead of billing
> you. That's the zero-risk configuration if you ever want it back.

### `check-lfs.ps1`

A script in the repo root that reports local LFS footprint alongside your current
billing cycle.

**One-time setup**

1. Install the GitHub CLI from https://cli.github.com
2. Authenticate: `gh auth login`
3. The token needs **Plan** read permission. If you authenticated through the browser
   flow, re-run `gh auth refresh -s read:project` or generate a fine-grained PAT with
   *Plan → Read-only* and `gh auth login --with-token`.
4. Windows blocks downloaded scripts by default. In PowerShell, from the repo root:

   ```powershell
   Unblock-File .\check-lfs.ps1
   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
   ```

   `RemoteSigned` allows local scripts you wrote while still blocking unsigned
   downloaded ones. It's the normal setting for a development machine.

**Running it**

```powershell
cd <repo root>
.\check-lfs.ps1
```

Output goes to the console in three blocks:

- **This repo** — how many files LFS tracks at HEAD, their total size, and the size of
  your local LFS object cache. That cache is a floor for how much history has piled up
  server-side.
- **GitHub billing cycle** — LFS line items for the current cycle and what they cost. If
  nothing prints, you're inside the free allowance, which is the expected result today.
- **Dashboard** — the billing link.

If the billing half fails, the local half still works — it needs no network and no auth.
The LFS product line may not appear in the API response until your account is on the
metered billing platform, so an empty remote section early on is normal rather than
broken.

**Automating it**

Windows Task Scheduler, weekly:

- Program: `powershell.exe`
- Arguments: `-NoProfile -ExecutionPolicy Bypass -File "C:\path\to\check-lfs.ps1"`
- Start in: the repo root

Append `*> C:\path\to\lfs-log.txt` to the arguments to keep a running log you can skim
for the growth curve rather than a single point in time.

---

## 6. Build and publish

### Package

Playtest builds use **Development**, not Shipping. Development keeps the console (`~`),
on-screen logging, and writes a log to `Saved/Logs` inside the packaged build — which is
how netcode bugs get diagnosed. Switch to Shipping only for people outside the team.

Everything below is wrapped in **`package.bat`** in the repo root. Run that rather than
assembling the command by hand:

```bat
package.bat
```

It refuses to run while the editor or a packaged `DinoGame.exe` is open, because both
lock files it has to overwrite.

### The editor target is built first, on purpose

`package.bat` builds `DinoGameEditor` before it cooks. This is not optional and it is not
obvious:

> **Cooking runs through `UnrealEditor-DinoGame.dll`.** If that module is stale relative
> to the C++, the cook writes Blueprints laid out for the old class while the packaged
> exe expects the new one. The build succeeds. The package then crashes on launch with
> `ObjectSerializationError: ... Bad export index`, before a window ever appears.

Any change to a C++ class a Blueprint inherits from — a new `UPROPERTY`, a new component,
a changed layout — requires this. Cost us an evening once.

The underlying command, for reference:

```bat
RunUAT.bat BuildCookRun ^
  -project="<path>.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Development ^
  -cook -build -stage -pak -compressed -archive ^
  -archivedirectory="<out>" ^
  -nocompileeditor -utf8output
```

### When a full package is required

| Change | Needs |
|---|---|
| C++ logic only, no layout change | Game target build; copy `DinoGame.exe` |
| New/changed `UPROPERTY`, component, class layout | **Full package** |
| Any `Config/*.ini` change | **Full package** — config is baked into the `.pak`, not staged loose |
| Blueprint or asset edits | **Full package** |

When in doubt, full package. The exe-only hot-swap is an optimization that is only safe
in the first row.

### Publish

```bat
butler push "<out>\Windows" <itch-user>/dino-game:windows-playtest
```

Add this as the last line of `package.bat` once the itch project exists, so shipping a
build is one double-click.

### Why itch and not the repo

The repo and the playable build are different artifacts for different audiences.

- **The repo** is the workshop. Only people who build the game need it, and it requires
  Unreal, a GitHub account, and the plugin.
- **itch** is the storefront. Playtest-only teammates double-click an exe. No Unreal, no
  Git, no repo access.

**Never ship packaged builds through Git.** They're derived artifacts. Committing them
would permanently inflate unprunable LFS history, and every download would burn
bandwidth quota.

### Delta patching, and what it does not do

`butler push` splits the build into blocks, compares against the build already on itch,
and uploads only changed blocks. itch's backend then re-diffs with bsdiff and
recompresses with Brotli to shrink the patch further. Teammates on the itch desktop app
receive only that patch, applied in place — a 12 GB game with a few changed assets can
be a ~150 MB update.

This helps the **download** side. It does nothing for artists pushing assets — but that
side needs no help: LFS already transfers only changed objects, and uploads don't touch
bandwidth quota at all.

Hard limit: itch rejects builds over **30 GB uncompressed**.

### Access

Set the itch project page to **Restricted** and add teammates by itch username or email.
The game stays private and you manage no passwords.

---

## 7. Multiplayer

**Current state (Aug 2026):** player-hosted listen server on Unreal's replication,
transported over **Steam Sockets / Steam Datagram Relay**, with session-based
host / find / join through `UDinoSessionSubsystem`. Proximity voice chat is implemented
and working. Verified between two packaged builds on two machines.

### NAT traversal is already solved

This section previously described NAT traversal as the open problem, because the plan was
to connect by direct IP. That is no longer how the game connects. From a real session log:

```
LogNet: SteamSocketsNetDriver started listening on 7777
LogNet: NotifyAcceptedConnection: RemoteAddr: 76561199071088019:7777,
        Name: SteamSocketsNetConnection, UniqueId: STEAM:<player>
```

**The remote address is a SteamID64, not an IP.** Peers are addressed by Steam identity
and Steam picks the route — direct LAN, NAT-punched UDP, or SDR relay fallback. The host
needs no port forwarding, no public IP, and never exposes their address.

That is exactly what adopting EOS was going to buy, so EOS and Tailscale are both off the
plan. See §1.

### What is still unproven

Both test machines were on the same LAN, so Steam almost certainly chose a direct local
route and **the relay path has not actually been exercised**. The code path is identical
either way — the game hands Steam a SteamID and route selection is Steam's problem — but
"identical code path" is an argument, not evidence.

> **Next milestone: one session with a player outside the local network.** This is the
> single highest-value test remaining and it needs no new code. It either confirms remote
> play or produces a concrete failure to chase.

### The AppID question

The project currently runs on `SteamDevAppId=480`, Valve's shared Spacewar test ID.

| | AppID 480 | Real Steamworks app ($100) |
|---|---|---|
| Cost and setup | None | $100, plus adding each tester in Steamworks |
| Who can run it | Anyone with any Steam account | Only users explicitly granted access |
| Session search | Returns other developers' Spacewar lobbies too | Only ours |
| Friend invites | Show as "Spacewar" | Show the real game |

For a handful of testers, 480 is genuinely less friction — there is no per-tester admin.
The real cost is lobby noise, which a **join code** makes irrelevant, since the client
filters by code instead of browsing a list. That is why join code is the chosen join
model over a server browser.

Move to a real AppID when approaching public release, or when the noise starts to bite.
It is a prerequisite for shipping on Steam regardless.

### Voice chat

Unreal's built-in VOIP, riding the same connection — no hosted voice service, so no new
central dependency. Open mic (voice-activated, no push-to-talk key), spatialised at the
speaker's position with distance falloff and occlusion through geometry.

Practical notes for playtests:

- **Every player needs a working microphone**, and Windows must allow desktop apps to
  access it. A dead capture device shows up as `LogVoiceCapture` errors in the log.
- Tuning lives in `Config/DefaultEngine.ini` under `[Voice]` and `[SystemSettings]`.
  `voice.JitterBufferDelay` trades latency against choppiness; the sample rate is set by
  `VoiPSampleRate` under `[/Script/Engine.AudioSettings]`, and its default of 16 kHz
  sounds like a telephone.
- Those values are read once at startup, so changing them needs a repackage, not a
  restart.

### Known gap before artists can play

Joining currently requires console commands (`DinoFind`, then `DinoJoin <index>`), with
the results only visible in the full console — `~` pressed twice. **This is not usable by
non-technical playtesters**, so a join-code menu is a prerequisite for a real playtest
session, not a later nicety.

---

## 8. Team rules

- **Lock before you work, submit before you sleep.** An overnight lock is fine. A
  week-long lock blocks someone who then stops asking.
- **Pull before you open the editor**, never after. Unreal caches aggressively and
  pulling underneath a running editor causes confusing corruption.
- **Everyone runs the identical engine version.** Mismatched versions silently re-save
  assets in a newer format that older editors can't open. This is the most common way a
  repo gets damaged and it has nothing to do with Git.
- **One build night, on a fixed day.** A predictable slot gets busy people into the same
  session far better than "whenever it's ready."
- **Bugs go in Notion, not the group chat.** Filing a row is the last step of every
  playtest session.
- **Source art on the share, engine-ready assets in the repo.** No exceptions — this is
  what keeps the repo from becoming unaffordable.
- **Steam must be running to play.** The build comes from itch, but the networking is
  Steam's. Unintuitive enough that it belongs on the list.

---

## 9. Onboarding

### For Sam, in order

1. Create the private GitHub repo. Add all seven collaborators.
2. Build the repo with UEGitPlugin's reference `.gitignore` / `.gitattributes`. Compile
   the plugin. Enable One File Per Actor. Record the exact engine version above.
3. Set the LFS budget and alerts.
4. Add the `backup` remote, set `remote.backup.lfsurl`, push all three commands, and run
   the restore verification.
5. Onboard **one** teammate — the most patient one — over a call. Fix whatever confused
   them before anyone else sees it.
6. **Get a session working with a player outside the local network.** No VPN, no new
   code — just a second Steam account on a different connection. This is the milestone
   that de-risks the project.
7. Create the restricted itch page, install butler, add the `butler push` line to
   `package.bat`, and push the first build.
8. Build the join-code menu. Until this exists, playtests need someone talking a tester
   through console commands.
9. Onboard the rest, spread across two calendar months if you want to stay inside the
   free LFS bandwidth allowance.

### For teammates — send them this part

1. **Install Unreal Engine 5.8.1** — that exact version. Not the newest one. This matters
   more than it sounds like it does. *(Only if you're opening the project. Playtesters
   don't need Unreal at all.)*
2. **Install the itch.io app** and accept the invite. This is how you get the playable
   game, and it updates itself.
3. **Have Steam installed and signed in** whenever you play. The game comes from itch,
   but it uses Steam to find and connect to other players. You don't need to own
   anything — any Steam account works.
4. **Plug in a microphone.** Voice chat is positional: people get quieter with distance
   and muffled through walls. If Windows can hear you, so can the game.
5. **Open the project** from the folder Sam sets up. You'll see lock icons on files.
6. **Before editing anything:** right-click → **Check Out**. Red check means it's yours.
   Blue check means someone else has it — message them, don't edit around it.
7. **When you're done for the day:** right-click → **Submit to Revision Control**, write
   one line about what changed. Your work is now saved for everyone.
8. **Artists:** work on your own machine as you always have. Export the finished
   engine-ready asset into the project and submit it. Your source files (PSD, ZBrush,
   Blender) don't go in the project — share them with Sam whenever you finish something
   and he'll file them in the archive. Don't worry about storage; it's on his account.
