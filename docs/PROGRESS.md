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
| 4 | Driver hardening (robustness, memory, static analysis, mutation) | done | `make test-all && make mutation`: lint + static clean, guest 90/90, 0 skipped, mutation 3/3 | `phase 4: Driver hardening` / `phase-4` |
| 5 | Thermostat control core and simulation | done | `make test-all`: host 52 unit tests x3 (normal/ASan+UBSan/Valgrind) + SIL checks, guest 90/90, 0 skipped | `phase 5: Thermostat control core and simulation` / `phase-5` |
| 6 | Thermostat daemon on the board (sensor, Modbus, end to end) | in progress | | |
| 7 | CI, metrics, documentation, publishing (v1.0.0) | not started | | |
| Ext 1 | Watchdog supervision | not started | | |
| Ext 2 | Fault detection and diagnostics | not started | | |
| Ext 3 | MQTT telemetry | not started | | |
| Ext 4 | Control core on FreeRTOS (v1.1.0) | not started | | |

"Gate result" example: `make test-all: host 42/42, guest 57/57, 0 skipped`.

## Current phase

Phase: 6 (Thermostat daemon on the board)

Phase 5 task checklist (done):

- [x] `thermostat/` CMake project (C11, strict flags, `HVAC_SANITIZE`, `HVAC_COVERAGE`)
- [x] Unity v2.7.0 vendored with license
- [x] `hvac_core.c` (section 4.4), `config.c` (parse string + load file, 8 KB); D-021, D-022
- [x] `test_hvac_core.c` (34), `test_config.c` (18)
- [x] Room simulator, baseline controller, metrics, `sil` (integer micro-degrees); D-023
- [x] `scripts/plot_sil.py` -> `docs/images/sil_hot_day.png`
- [x] `scripts/test_host.sh`: lint, cppcheck, unit x3, coverage thresholds, SIL + checks
- [x] Gate; learning note `docs/phases/phase-05-control-core.md`

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
- Phase 4: guest 90/90; kmemleak/KASAN/lockdep reports 0/0/0; concurrent read errors 0; 500 load/unload
  cycles; checkpatch 0/0, sparse 0; mutation 3/3; read latency median ~1.1 ms (emulated).
- Phase 5: host unit tests 52 (normal, ASan+UBSan, Valgrind all clean); cppcheck 0; coverage hvac_core.c
  100 % lines / 98.9 % branches, config.c 98.1 % lines; SIL hot day product 2.83 starts/h vs baseline
  592.2, 0 short cycles, comfort 100 % in all scenarios.

## Publishing

- Repository URL: (Phase 7)
- Last pushed commit:
- Tags on remote:
- Last CI run and result:
