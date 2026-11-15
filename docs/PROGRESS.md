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
| 2 | Driver skeleton and hello module | done | `make test-all`: host lint clean, guest 40/40, 0 skipped | `phase 2: Driver skeleton and hello module` / `phase-2` |
| 3 | Complete driver (IIO, conversion, alarms, KUnit) | done | `make test-all`: host lint clean, guest 83/83, 0 skipped | `phase 3: Complete driver` / `phase-3` |
| 4 | Driver hardening (robustness, memory, static analysis, mutation) | in progress | | |
| 5 | Thermostat control core and simulation | not started | | |
| 6 | Thermostat daemon on the board (sensor, Modbus, end to end) | not started | | |
| 7 | CI, metrics, documentation, publishing (v1.0.0) | not started | | |
| Ext 1 | Watchdog supervision | not started | | |
| Ext 2 | Fault detection and diagnostics | not started | | |
| Ext 3 | MQTT telemetry | not started | | |
| Ext 4 | Control core on FreeRTOS (v1.1.0) | not started | | |

"Gate result" example: `make test-all: host 42/42, guest 57/57, 0 skipped`.

## Current phase

Phase: 4 (Driver hardening)

Phase 3 task checklist (done):

- [x] `driver/hvac_temp_conv.h` exactly as plan 4.3
- [x] IIO channel, `read_raw` (RAW under mutex, SCALE constant), `devm_iio_device_register`
- [x] `driver/hvac_temp_alarm.c` (two attributes via IIO attrs group, under the mutex)
- [x] `driver/hvac_temp_kunit.c` (suite `hvac_temp_conv`, 5 cases), separate module
- [x] Tests: conversion (12), attributes (5), alarms (25), KUnit (1); D-016, D-017
- [x] Read-error path limitation documented (D-018)
- [x] Gate `make test-all`; learning note `docs/phases/phase-03-complete-driver.md`

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
- Phase 2: guest 40/40; 50 bind cycles in 13 s.
- Phase 3: guest 83/83; KUnit 5/5 cases (`kunit_cases_passed` = 5).

## Publishing

- Repository URL: (Phase 7)
- Last pushed commit:
- Tags on remote:
- Last CI run and result:
