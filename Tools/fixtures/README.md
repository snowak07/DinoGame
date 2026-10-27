Logs with known findings, for `python Tools/check_logs.py --self-test` (run by `Tools/run_tests.py`).

- `ensure.log`, `host_left.log`, `client_disconnected.log`: cut from real playtest logs (October 2026), so the line formats are the engine's own.
- `selfcheck_fail.log`: `host_left.log` with one self-check failure written in, in the format `DinoSelfCheck.cpp` logs.
- `dino_error.log`: cut from a real log, an Error our own code logged (a raptor spinning on an unreachable spot).
- `format_drift.log`: an ensure in a made-up future log format. The checker must notice it could not parse it.

When an engine upgrade changes a log format, add a real excerpt here and a line to `self_test()`.
