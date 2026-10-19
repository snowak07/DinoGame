"""
Builds the editor target and runs DinoGame's unit tests headless, then prints the results.
Also runs the log checker's self-test first (seconds), so the tool that judges playtests is
itself known to work.

    python Tools/run_tests.py              build, then run every DinoGame.* test
    python Tools/run_tests.py --no-build   run against the current editor build
    python Tools/run_tests.py DinoGame.Voice   run one group (any test-name prefix)

Needs the editor closed: Live Coding locks the editor target's DLL, so the build fails while
it is open. Exit code 0 when every test passed.

The tests are in Source/DinoGameTests (an editor-only module, never packaged). The same tests
appear in the editor under Tools -> Session Frontend -> Automation, as "DinoGame".
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

ENGINE = Path(r"H:\Unreal\UE_5.8\Engine")
PROJECT_DIR = Path(__file__).resolve().parent.parent
PROJECT = PROJECT_DIR / "DinoGame.uproject"
LOG_DIR = PROJECT_DIR / "Saved" / "Logs"
LOG_NAME = "DinoGameTests.log"

RESULT_RE = re.compile(r"Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\} Path=\{([^}]*)\}")
ERROR_RE = re.compile(r"LogAutomationController: (?:Error|Warning): (.*)")


def editor_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe"], capture_output=True, text=True).stdout
    return "UnrealEditor.exe" in out


def build():
    print("Building DinoGameEditor...")
    result = subprocess.run([
        str(ENGINE / "Build" / "BatchFiles" / "Build.bat"),
        "DinoGameEditor", "Win64", "Development", f"-Project={PROJECT}", "-WaitMutex",
    ], capture_output=True, text=True)
    output = result.stdout + result.stderr
    errors = [line for line in output.splitlines() if " error " in line or ": error" in line or "Result: Failed" in line]
    if result.returncode != 0:
        print("\n".join(errors[-25:]) or output[-3000:])
        print("BUILD FAILED")
        return False
    print("Build succeeded.")
    return True


def run(filter_prefix):
    log_path = LOG_DIR / LOG_NAME
    if log_path.exists():
        log_path.unlink()

    command = [
        str(ENGINE / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe"),
        str(PROJECT),
        f"-ExecCmds=Automation RunTests {filter_prefix}",
        "-TestExit=Automation Test Queue Empty",
        "-unattended", "-nopause", "-nosplash", "-NullRHI", "-NoSound",
        f"-abslog={log_path}",
    ]
    print(f"Running tests matching '{filter_prefix}' headless...")
    started = time.time()
    subprocess.run(command, capture_output=True, text=True)
    print(f"Finished in {time.time() - started:.0f} s. Log: {log_path}")

    if not log_path.exists():
        print("NO LOG WRITTEN - the editor did not start. Check that the editor target is built.")
        return False

    log = log_path.read_text(encoding="utf-8", errors="replace")
    results = RESULT_RE.findall(log)
    if not results:
        print("NO TESTS RAN. Either the filter matched nothing, or the DinoGameTests module did not load.")
        return False

    failed = [(name, path) for outcome, name, path in results if outcome != "Success"]
    for outcome, name, path in results:
        print(f"  {'PASS' if outcome == 'Success' else outcome.upper():8} {path}")

    if failed:
        print("\nFailure details:")
        for line in log.splitlines():
            match = ERROR_RE.search(line)
            if match:
                print(f"  {match.group(1)}")

    # A pass with an ensure or error behind it is not clean: the code under test misbehaved in a
    # way no assertion asked about. Test failures themselves are already listed above.
    stray = [line for line in log.splitlines()
             if ("Ensure condition failed" in line or " Error: " in line) and "LogAutomationController" not in line]
    if stray:
        print(f"\n{len(stray)} error/ensure line(s) in the run outside the test results:")
        for line in stray[:20]:
            print(f"  {line[:200]}")

    print(f"\nRESULT: {len(results) - len(failed)} passed, {len(failed)} failed"
          + (f", {len(stray)} stray error(s) - NOT CLEAN" if stray else ""))
    return not failed and not stray


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("filter", nargs="?", default="DinoGame", help="test-name prefix (default: DinoGame)")
    parser.add_argument("--no-build", action="store_true", help="skip building the editor target")
    args = parser.parse_args()

    if editor_running() and not args.no_build:
        print("The editor is open. Close it first: Live Coding blocks the editor build.")
        return 1

    print("Log checker self-test...")
    checker = subprocess.run([sys.executable, str(Path(__file__).parent / "check_logs.py"), "--self-test"], capture_output=True, text=True)
    if checker.returncode != 0:
        print(checker.stdout + checker.stderr)
        print("LOG CHECKER SELF-TEST FAILED")
        return 1
    print("  " + checker.stdout.strip().splitlines()[-1])

    if not args.no_build and not build():
        return 1
    return 0 if run(args.filter) else 1


if __name__ == "__main__":
    sys.exit(main())
