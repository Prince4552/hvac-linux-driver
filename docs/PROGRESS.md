# Progress

This file is the project's memory. Keep it accurate at all times, so that a new session can continue
the work without the chat history. Update it at the start and end of every phase, and whenever you hit
a blocker.

## Status

Status values: `not started`, `in progress`, `blocked`, `done`.

| Phase | Title | Status | Gate result | Commit / tag |
| --- | --- | --- | --- | --- |
| 0 | Foundation | done | `make check-env && make venv && make lint`: all pass | `phase 0: Foundation` / `phase-0` |
| 1 | Platform (Buildroot + QEMU + autotest) | in progress | | |
| 2 | Driver skeleton and hello module | not started | | |
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

Phase: 1 (Platform)

Phase 0 task checklist (done):

- [x] `git init -b main`; git identity present
- [x] Layout from PROJECT_PLAN.md section 3 (folder READMEs, `.gitkeep` in empty sub-folders, D-006)
- [x] `.gitignore`, `LICENSE`, `THIRD_PARTY.md`, `VERSIONS.md`, `requirements-host.txt` (full pins, D-004)
- [x] `scripts/check_env.sh`
- [x] `scripts/setup_host_venv.sh`
- [x] `scripts/lint.sh`
- [x] `Makefile` with `check-env`, `venv`, `lint` and failing placeholders
- [x] `README.md` with work-in-progress note
- [x] Gate `make check-env && make venv && make lint`
- [x] Learning note `docs/phases/phase-00-foundation.md`

## Blockers

None.

(Format when blocked: date, phase, what you were doing, exact error, what you tried (3 attempts),
log file path, what you need from the user.)

## Key numbers so far

(Fill in as phases complete: test counts, coverage, memory-check results, SIL metrics, build time.)

- Phase 0: check-env all pass; lint 3 shell files clean.

## Publishing

- Repository URL: (Phase 7)
- Last pushed commit:
- Tags on remote:
- Last CI run and result:
