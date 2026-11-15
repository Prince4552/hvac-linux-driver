# Progress

This file is the project's memory. Keep it accurate at all times, so that a new session can continue
the work without the chat history. Update it at the start and end of every phase, and whenever you hit
a blocker.

## Status

Status values: `not started`, `in progress`, `blocked`, `done`.

| Phase | Title | Status | Gate result | Commit / tag |
| --- | --- | --- | --- | --- |
| 0 | Foundation | done | `make check-env && make venv && make lint`: all pass | `phase 0: Foundation` / `phase-0` |
| 1 | Platform (Buildroot + QEMU + autotest) | done | `make test-all`: host lint clean, guest 34/34, 0 skipped | `phase 1: Platform` / `phase-1` |
| 2 | Driver skeleton and hello module | in progress | | |
| 3 | Complete driver (IIO, conversion, alarms, KUnit) | not started | | |
| 4 | Driver hardening (robustness, memory, static analysis, mutation) | not started | | |
| 5 | Thermostat control core and simulation | not started | | |
| 6 | Thermostat daemon on the board (sensor, Modbus, end to end) | not started | | |
| 7 | CI, metrics, documentation, publishing (v1.0.0) | not started | | |
| Ext 1 | Watchdog supervision | not started | | |
| Ext 2 | Fault detection and diagnostics | not started | | |
| Ext 3 | MQTT telemetry | not started | | |
| Ext 4 | Control core on FreeRTOS (v1.1.0) | not started | | |

"Gate result" example: `make test-all: host 42/42, guest 57/57, 0 skipped`.

## Current phase

Phase: 2 (Driver skeleton and hello module)

Phase 1 task checklist (done):

- [x] `scripts/fetch_buildroot.sh` (Buildroot 2026.02.3, SHA-256 checked, D-007), `br2-external/` skeleton
- [x] Defconfig (D-008), kernel fragment (+`CONFIG_DEBUG_KERNEL`, D-012), overlay + init scripts, `post-build.sh`
- [x] `scripts/build.sh`, `scripts/run_qemu.sh`, `scripts/test_qemu.sh`, shared `common.sh` / `qemu_common.sh` (D-009, D-010)
- [x] Guest runner, `hvac_test_lib.py`, `conftest.py`, `make_summary.py`; host `extract_guest_results.py`
- [x] `scripts/test_host.sh` (lint) and Makefile targets `build`, `run`, `test-host`, `test-qemu`, `test-all`
- [x] `tests/guest/test_smoke.py` (34 cases), boot-log exception D-014
- [x] Metric `guest_boot_to_tests_s`
- [x] Gate `make test-all`; learning note `docs/phases/phase-01-platform.md`

Work prepared ahead (not committed yet, belongs to later phases): Phase 2/3/4 driver code and tests
staged; `thermostat/` core, config, simulator and unit tests (Phase 5); daemon modules, Modbus client and
host daemon tests (Phase 6). Each is committed only with its own phase gate.

## Blockers

None.

(Format when blocked: date, phase, what you were doing, exact error, what you tried (3 attempts),
log file path, what you need from the user.)

## Key numbers so far

(Fill in as phases complete: test counts, coverage, memory-check results, SIL metrics, build time.)

- Phase 0: check-env all pass; lint 3 shell files clean.
- Phase 1: first full Buildroot build 13.8 min; kernel Image 38 MB; rootfs 54 MB used; boot to tests ~9 s;
  guest 34/34.

## Publishing

- Repository URL: (Phase 7)
- Last pushed commit:
- Tags on remote:
- Last CI run and result:
