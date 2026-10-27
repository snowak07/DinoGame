"""
Checks packaged-build logs from both test machines for regressions, and summarises each run.

    python Tools/check_logs.py                 latest log from each machine
    python Tools/check_logs.py --backups 3     ...plus the 3 most recent backups from each
    python Tools/check_logs.py PATH [PATH...]  specific logs or Logs folders (LABEL=PATH to name one)
    python Tools/check_logs.py --self-test     check this script against Tools/fixtures

Per log: build, when it ran, how it ended, crashes and ensures (with the first game frame of
each stack), DinoSelfCheck results, disconnects, and the most frequent errors and warnings.

Only logs from the NEWEST build are judged. Older ones are summarised as history, so a problem
that has since been fixed stops failing the check once a newer build has been tested.

A disconnect is explained when another machine's log shows the host leaving or closing just
before it; unexplained ones need attention. The checker also counts its markers in the raw text
and reports when its own parsing missed any - the sign an engine update changed a log format.

Exit code 0 when clean, 1 when anything needs looking at. Read by Claude after every test
session (CLAUDE.md, Testing); written to be skimmed by a person too.
"""

import argparse
import datetime as dt
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

DEFAULT_LOG_DIRS = [
    ("local", Path(r"H:\Unreal\Projects\Builds\Windows\DinoGame\Saved\Logs")),
    ("SOPHIE", Path(r"\\Sophie\DinoGame\DinoGame\Saved\Logs")),
]
FIXTURES = Path(__file__).resolve().parent / "fixtures"

LINE_RE = re.compile(r"^\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}):(\d{3})\]\[\s*\d+\](\w+): (?:(Error|Warning|Display|Verbose): )?(.*)$")
CLOSED_RE = re.compile(r"^\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}):(\d{3})\]\[\s*\d+\]Log file closed")

CRASH_MARKERS = ("Fatal error!", "=== Critical error: ===", "Unhandled Exception", "Assertion failed:", "appError called")
ENSURE_MARKER = "Ensure condition failed:"
DISCONNECT_MARKER = "BroadcastNetworkFailure: FailureType = ConnectionLost"

# Events on one machine that explain another machine losing its connection shortly after.
LEAVE_MARKERS = ("Left session", "Log file closed", "LogExit: Exiting.")
DISCONNECT_WINDOW = dt.timedelta(seconds=20)

# Frequent, understood, and not ours to fix. Kept out of the "most frequent" lists so new
# problems stand out; still counted in the totals.
KNOWN_NOISE = (
    "Context Requirements in UStateTreeComponent::StopLogic failed",
    "Unable to find RecastNavMesh instance while trying to create UCrowdManager",
    "SteamNetworkingSockets lock",
    "Console variable 'r.MotionVectorSimulation' used in the render thread",
    "Failed to load 'aqProf.dll'",
    "File 'aqProf.dll' does not exist",
)

GAME_FRAME_RE = re.compile(r"DinoGame\.exe!(\w*Dino\w*::\w+)")


def normalise(message):
    """Strip numbers, addresses and object suffixes so repeats of one message group together."""
    message = re.sub(r"0x[0-9A-Fa-f]+", "0x?", message)
    message = re.sub(r"_C_\d+", "_C_?", message)
    message = re.sub(r"\d+(\.\d+)?", "#", message)
    return message[:160]


def parse_time(stamp, millis="000"):
    when = dt.datetime.strptime(stamp, "%Y.%m.%d-%H.%M.%S").replace(tzinfo=dt.timezone.utc)
    return when + dt.timedelta(milliseconds=int(millis))


class LogReport:
    def __init__(self, machine, path):
        self.machine = machine
        self.path = path
        self.builds = []
        self.first_time = None
        self.last_time = None
        self.closed_cleanly = False
        self.crashes = []            # (time, message, game frame)
        self.ensures = []
        self.selfcheck_fails = Counter()
        self.selfcheck_runs = 0
        self.selfcheck_failed_runs = 0
        self.errors = Counter()
        self.warnings = Counter()
        self.error_total = 0
        self.warning_total = 0
        self.dino_errors = []        # (time, message): errors our own code logged - always a problem
        self.restarts = []           # times a seamless travel (round restart, back to lobby) began
        self.reached_round = False   # a round actually started on this machine
        self.voices_released = []    # (time, count) at each map change and shutdown, from the cleanup log
        self.disconnects = []        # times
        self.leaves = []             # (time, what) - events that explain another machine's disconnect
        self.raw = Counter()         # marker counts in the raw text, to catch parser misses
        self.judged = True
        self.explained = {}          # disconnect time -> explanation

    # --- parsing -------------------------------------------------------------------------

    def parse(self):
        lines = self.path.read_text(encoding="utf-8", errors="replace").splitlines()
        pending = None  # (kind, time, message) awaiting its first game frame

        for line in lines:
            self.count_raw(line)

            closed = CLOSED_RE.match(line)
            if closed:
                self.closed_cleanly = True
                when = parse_time(*closed.groups())
                self.leaves.append((when, "log closed"))
                self.last_time = when
                continue

            match = LINE_RE.match(line)
            if not match:
                continue
            stamp, millis, category, verbosity, message = match.groups()
            when = parse_time(stamp, millis)
            self.first_time = self.first_time or when
            self.last_time = when

            # A stack is the run of [Callstack] lines after the marker. The first game frame names it.
            if pending:
                if "[Callstack]" in message:
                    frame = GAME_FRAME_RE.search(message)
                    if frame:
                        self.record(pending, frame.group(1))
                        pending = None
                    continue
                if category != "LogOutputDevice":
                    self.record(pending, "(engine only)")
                    pending = None

            if ENSURE_MARKER in message:
                pending = ("ensure", when, message.split(ENSURE_MARKER, 1)[1].split("[File:")[0].strip())
                continue
            if any(marker in message for marker in CRASH_MARKERS):
                pending = ("crash", when, message.strip()[:200])
                continue

            self.read_event(when, category, verbosity, message)

        if pending:
            self.record(pending, "(no stack)")
        return self

    def count_raw(self, line):
        if ENSURE_MARKER in line:
            self.raw["ensure"] += 1
        if any(marker in line for marker in CRASH_MARKERS):
            self.raw["crash"] += 1
        if "DinoSelfCheck[" in line and ": FAIL " in line:
            self.raw["selfcheck fail"] += 1
        if DISCONNECT_MARKER in line:
            self.raw["disconnect"] += 1

    def record(self, pending, frame):
        kind, when, message = pending
        (self.ensures if kind == "ensure" else self.crashes).append((when, message, frame))

    def read_event(self, when, category, verbosity, message):
        if category == "LogDinoGame" and message.startswith("Build version:"):
            version = message.split(":", 1)[1].strip()
            if not self.builds or self.builds[-1] != version:
                self.builds.append(version)

        if category in ("LogGameState", "LogGameMode") and "Match State Changed from WaitingToStart to InProgress" in message:
            self.reached_round = True

        # Logged by host and client alike, unlike the game mode's per-player handoff lines.
        if category == "LogWorld" and message.startswith("SeamlessTravel to:"):
            self.restarts.append(when)

        released = re.match(r"Voice: released (\d+) remote speaker", message)
        if released:
            self.voices_released.append((when, int(released.group(1))))

        if message.startswith("DinoSelfCheck["):
            if ": FAIL " in message:
                # "<name> | <detail>"; logs from before the separator used the first colon.
                failure = message.split(": FAIL ", 1)[1]
                name = failure.split(" | ", 1)[0] if " | " in failure else failure.split(":", 1)[0]
                self.selfcheck_fails[name.strip()] += 1
            elif re.search(r"\d+ passed, \d+ failed", message):
                self.selfcheck_runs += 1
                if not re.search(r", 0 failed", message):
                    self.selfcheck_failed_runs += 1

        if DISCONNECT_MARKER in message:
            self.disconnects.append(when)
            return
        if category == "LogNet" and "ConnectionLost" in message:
            return  # the follow-up lines of the same disconnect

        for marker in LEAVE_MARKERS:
            if marker in f"{category}: {message}":
                self.leaves.append((when, marker.replace("LogExit: ", "").rstrip(".").lower()))

        # The ensure and crash printouts themselves, already reported above.
        if category == "LogOutputDevice":
            return

        if verbosity == "Error":
            self.error_total += 1
            if not any(noise in message for noise in KNOWN_NOISE):
                self.errors[f"{category}: {normalise(message)}"] += 1
            # Our code logs Error only for something it believes is wrong. An engine error can be
            # noise; one of ours never is, so it is judged rather than just counted.
            if category.startswith("LogDino"):
                self.dino_errors.append((when, message.strip()[:200]))
        elif verbosity == "Warning":
            self.warning_total += 1
            if not any(noise in message for noise in KNOWN_NOISE) and "DinoSelfCheck" not in message:
                self.warnings[f"{category}: {normalise(message)}"] += 1

    # --- judging -------------------------------------------------------------------------

    def parser_misses(self):
        """Markers in the raw text that parsing did not account for."""
        parsed = {"ensure": len(self.ensures), "crash": len(self.crashes),
                  "selfcheck fail": sum(self.selfcheck_fails.values()), "disconnect": len(self.disconnects)}
        return {kind: (self.raw[kind], parsed[kind]) for kind in parsed if self.raw[kind] != parsed[kind]}

    def problems(self):
        found = []
        if self.crashes:
            found.append(f"{len(self.crashes)} crash(es)")
        if self.ensures:
            found.append(f"{len(self.ensures)} ensure(s)")
        if self.dino_errors:
            found.append(f"{len(self.dino_errors)} error(s) from our own code")
        if self.selfcheck_fails:
            found.append(f"{sum(self.selfcheck_fails.values())} self-check failure(s)")
        unexplained = [t for t in self.disconnects if t not in self.explained]
        if unexplained:
            found.append(f"{len(unexplained)} unexplained disconnect(s)")
        if self.parser_misses():
            found.append("checker missed markers - update check_logs.py")
        if self.first_time is None:
            found.append("no timestamped lines - unreadable, or the log format changed")
        if self.reached_round and self.selfcheck_runs == 0:
            found.append("a round was played but DinoSelfCheck never ran - it is broken or was removed")
        return found

    def describe_ending(self):
        if self.closed_cleanly:
            return "cleanly"
        # The engine writes the log from a background thread, so a game that is running or still
        # shutting down has not written its last lines yet. Read again once it has exited.
        age = dt.datetime.now().timestamp() - self.path.stat().st_mtime
        if age < 60:
            return f"still being written ({age:.0f} s ago) - the game is running or shutting down; check again once it has exited"
        return "WITHOUT closing the log - a crash or a forced close"

    def restart_voice_coverage(self):
        """
        (exercised, text): whether a restart happened while a remote voice was playing - the path
        that crashed, then stuttered, and the only one that proves the voice cleanup. A release
        belongs to a restart when it comes within seconds of one; otherwise it was leaving or
        quitting. None when this build did not log every cleanup.
        """
        during = [n for when, n in self.voices_released
                  if any(start <= when <= start + dt.timedelta(seconds=10) for start in self.restarts)]
        live = sum(1 for n in during if n > 0)
        prefix = f"cleanup logged {len(self.voices_released)}x, {len(during)} at restarts; "
        if live:
            return True, prefix + f"voices live at {live} restart(s) - restart-while-talking path exercised"
        if len(during) < len(self.restarts):
            return None, prefix + "not logged at every restart (a build from before it always logged) - unknown whether anyone was talking"
        return False, prefix + "nobody was talking at any restart - restart-while-talking path NOT exercised"

    # --- output --------------------------------------------------------------------------

    def print(self, top):
        span = ""
        if self.first_time and self.last_time:
            span = f"{self.first_time:%Y-%m-%d %H:%M} UTC, {(self.last_time - self.first_time).total_seconds() / 60:.0f} min"
        status = "" if self.judged else "  [older build: history only, not judged]"
        print(f"\n=== {self.machine}: {self.path.name}  ({span}){status}")
        print(f"  build      : {', '.join(self.builds) or 'unknown'}")
        print(f"  ended      : {self.describe_ending()}")
        print(f"  restarts   : {len(self.restarts)} (seamless travels)")
        if self.voices_released:
            print(f"  voice      : {self.restart_voice_coverage()[1]}")

        for label, items in (("CRASHES", self.crashes), ("ENSURES", self.ensures)):
            if items:
                print(f"  {label}    : {len(items)}")
                for when, message, frame in items:
                    print(f"    {when:%H:%M:%S}  {message}  <- {frame}")

        if self.selfcheck_runs:
            print(f"  self-check : {self.selfcheck_runs} runs, {self.selfcheck_failed_runs} with failures")
        else:
            print("  self-check : never ran (older build, or never reached a match)")
        for name, count in self.selfcheck_fails.most_common():
            print(f"    FAIL x{count}  {name}")

        for when, message in self.dino_errors:
            print(f"  OUR ERROR  : {when:%H:%M:%S}  {message}")

        for when in self.disconnects:
            why = self.explained.get(when)
            print(f"  disconnect : {when:%H:%M:%S}  " + (f"expected - {why}" if why else "UNEXPLAINED: no other log shows the host leaving"))

        for kind, (raw, parsed) in self.parser_misses().items():
            print(f"  CHECKER    : {raw} '{kind}' marker(s) in the text, {parsed} parsed - its parsing is out of date")

        print(f"  errors     : {self.error_total} total" + (", most frequent:" if self.errors else ""))
        for message, count in self.errors.most_common(top):
            print(f"    x{count:<4} {message}")
        print(f"  warnings   : {self.warning_total} total" + (", most frequent (known noise hidden):" if self.warnings else ""))
        for message, count in self.warnings.most_common(top):
            print(f"    x{count:<4} {message}")


# --- across logs -------------------------------------------------------------------------

def explain_disconnects(reports):
    """A disconnect is expected when another machine left or closed just before it."""
    for report in reports:
        for when in report.disconnects:
            for other in reports:
                if other is report:
                    continue
                for left, what in other.leaves:
                    if left <= when <= left + DISCONNECT_WINDOW:
                        report.explained[when] = f"{other.machine} {what} at {left:%H:%M:%S}, {(when - left).total_seconds():.0f} s earlier"
                        break
                if when in report.explained:
                    break


def mark_judged(reports):
    """Judge only logs that ran the newest build; the rest are history."""
    dated = [r for r in reports if r.builds and r.first_time]
    if not dated:
        return None
    newest = max(dated, key=lambda r: r.first_time).builds[-1]
    for report in reports:
        report.judged = newest in report.builds or not report.builds
    return newest


def crash_folders(reports):
    """Crash-report folders written while a judged run was going."""
    windows = defaultdict(list)
    for report in reports:
        if report.judged and report.first_time and report.last_time:
            windows[report.path.parent.parent].append((report.first_time, report.last_time))

    hits = []
    for saved, spans in windows.items():
        crashes = saved / "Crashes"
        if not crashes.exists():
            continue
        for folder in crashes.iterdir():
            made = dt.datetime.fromtimestamp(folder.stat().st_mtime, tz=dt.timezone.utc)
            if any(start - dt.timedelta(minutes=1) <= made <= end + dt.timedelta(minutes=2) for start, end in spans):
                hits.append((made, folder))
    return sorted(hits)


def analyse(reports):
    """Cross-log judging. Returns the list of things needing attention."""
    newest = mark_judged(reports)
    explain_disconnects(reports)

    attention = []
    for report in reports:
        if report.judged:
            attention += [f"{report.machine}/{report.path.name}: {problem}" for problem in report.problems()]

    latest = {}
    for report in reports:
        if report.path.name == "DinoGame.log" and report.builds:
            latest[report.machine] = report.builds[-1]
    if len(set(latest.values())) > 1:
        attention.append(f"build mismatch {latest}")

    folders = crash_folders(reports)
    if folders:
        attention.append(f"{len(folders)} crash-report folder(s)")
    return newest, latest, folders, attention


def coverage_gaps(reports):
    """Risky paths this build's test session never exercised. Not failures - things still unproven."""
    judged = [r for r in reports if r.judged and r.restarts]
    gaps = []
    if judged:
        results = [r.restart_voice_coverage()[0] for r in judged]
        if True not in results:
            gaps.append("a restart while someone was talking (voice cleanup on travel)"
                        + (" - this build did not log it" if None in results and False not in results else ""))
    return gaps


# --- input -------------------------------------------------------------------------------

def pick_logs(folder, backups):
    if not folder.exists():
        print(f"missing: {folder}", file=sys.stderr)
        return []
    current = folder / "DinoGame.log"
    logs = [current] if current.exists() else []
    older = sorted(folder.glob("DinoGame-backup-*.log"), key=lambda p: p.stat().st_mtime, reverse=True)
    return logs + older[:backups]


def collect(paths, backups):
    found = []
    for raw in paths:
        label, _, path_text = raw.rpartition("=") if "=" in raw and not raw.startswith("\\\\") else ("", "", raw)
        path = Path(path_text)
        if path.is_dir():
            found += [(label or str(path), log) for log in pick_logs(path, backups)]
        elif path.exists():
            found.append((label or path.parent.name, path))
        else:
            print(f"not found: {path}", file=sys.stderr)
    if paths:
        return found

    for machine, folder in DEFAULT_LOG_DIRS:
        try:
            found += [(machine, log) for log in pick_logs(folder, backups)]
        except OSError as error:
            print(f"{machine}: cannot read {folder} ({error})", file=sys.stderr)
    return found


# --- self-test ---------------------------------------------------------------------------

def self_test():
    """The checker against logs whose findings are known. Cut from real logs, so formats match."""
    def run(*pairs):
        reports = [LogReport(machine, FIXTURES / name).parse() for machine, name in pairs]
        return reports, analyse(reports)[3]

    failures = []

    def expect(what, condition):
        print(f"  {'PASS' if condition else 'FAIL'}  {what}")
        if not condition:
            failures.append(what)

    print("check_logs.py self-test:")

    (ensure,), attention = run(("A", "ensure.log"))
    expect("finds the ensure", len(ensure.ensures) == 1)
    expect("names the game frame it came from", ensure.ensures and ensure.ensures[0][2] == "UDinoVoiceCleanupSubsystem::ReleaseAllSpeakerAudio")
    expect("names the failed condition", ensure.ensures and ensure.ensures[0][1] == "IsInAudioThread()")
    expect("an ensure needs attention", any("ensure" in a for a in attention))
    expect("ensure printout lines are not also counted as errors", ensure.error_total == 0)

    reports, attention = run(("host", "host_left.log"), ("client", "client_disconnected.log"))
    host, client = reports
    expect("reads the build", host.builds == ["9d9267d-dirty"])
    expect("counts self-check lines", client.selfcheck_fails == Counter() and host.raw["selfcheck fail"] == 0)
    expect("finds the client's disconnect", len(client.disconnects) == 1)
    expect("explains it by the host leaving", client.disconnects and "host left session" in client.explained.get(client.disconnects[0], ""))
    expect("a host leaving is not a problem", attention == [])

    _, attention = run(("client", "client_disconnected.log"))
    expect("a disconnect with no host log is unexplained", any("unexplained disconnect" in a for a in attention))

    reports, attention = run(("A", "ensure.log"), ("host", "host_left.log"), ("client", "client_disconnected.log"))
    expect("an older build's ensure is history, not judged", not reports[0].judged and attention == [])

    (failed,), attention = run(("A", "selfcheck_fail.log"))
    expect("finds a self-check failure by name", failed.selfcheck_fails == Counter({"Voice registered: minerva": 1}))
    expect("counts the failed run", failed.selfcheck_runs == 1 and failed.selfcheck_failed_runs == 1)
    expect("a self-check failure needs attention", any("self-check failure" in a for a in attention))

    (silent_round,), attention = run(("A", "round_no_selfcheck.log"))
    expect("notices a round was played", silent_round.reached_round)
    expect("a round with no self-check run is a problem", any("DinoSelfCheck never ran" in a for a in attention))

    (talking,), _ = run(("A", "restart_talking.log"))
    expect("counts restarts", len(talking.restarts) == 2)
    expect("a live voice at a restart exercises the cleanup path", talking.restart_voice_coverage()[0] is True)
    expect("no coverage gap once exercised", coverage_gaps([talking]) == [])

    (silent,), attention = run(("A", "restart_silent.log"))
    expect("no live voice at any restart is not exercised", silent.restart_voice_coverage()[0] is False)
    expect("...and is reported as a coverage gap", len(coverage_gaps([silent])) == 1)
    expect("...but is not a failure", attention == [])

    (ours,), attention = run(("A", "dino_error.log"))
    expect("finds an error our own code logged", len(ours.dino_errors) == 1)
    expect("an error from our own code needs attention", any("our own code" in a for a in attention))

    (drifted,), attention = run(("A", "format_drift.log"))
    expect("notices markers its parsing missed", "ensure" in drifted.parser_misses())
    expect("reports that as needing attention", any("checker missed" in a for a in attention))

    print(f"RESULT: {'all passed' if not failures else f'{len(failures)} failed'}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="log files or Logs folders, optionally LABEL=PATH (default: both test machines)")
    parser.add_argument("--backups", type=int, default=0, help="also check this many recent backup logs per folder")
    parser.add_argument("--top", type=int, default=8, help="how many distinct errors and warnings to list")
    parser.add_argument("--self-test", action="store_true", help="check this script against Tools/fixtures")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    reports = [LogReport(machine, path).parse() for machine, path in collect(args.paths, args.backups)]
    if not reports:
        print("No logs found.")
        return 1

    newest, latest, folders, attention = analyse(reports)
    for report in reports:
        report.print(args.top)

    print("\n=== Across logs")
    print(f"  judging    : build {newest} (older builds shown as history)")
    if len(set(latest.values())) > 1:
        print(f"  BUILD MISMATCH: {latest}  - mismatched builds connect, then drop with 'closed by peer'")
    elif latest:
        print(f"  builds match: {next(iter(latest.values()))}")
    for made, folder in folders:
        print(f"  crash-report folder {made:%Y-%m-%d %H:%M} UTC: {folder}")

    gaps = coverage_gaps(reports)
    if gaps:
        print("\nNot yet exercised by this build (test these next session):\n  - " + "\n  - ".join(gaps))

    print("\nRESULT: " + ("needs attention:\n  - " + "\n  - ".join(attention) if attention else "clean"))
    return 1 if attention else 0


if __name__ == "__main__":
    sys.exit(main())
