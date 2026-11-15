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
| 6 | Thermostat daemon on the board (sensor, Modbus, end to end) | done | `make test-all`: host 76/76 (67 unit x3 + 9 integration), guest 98/98, 0 skipped | `phase 6: Thermostat daemon on the board` / `phase-6` |
| 7 | CI, metrics, documentation, publishing (v1.0.0) | in progress | `make test-all && make mutation && make metrics`: host 76/76, guest 98/98, 0 skipped, mutation 3/3, 41/41 metrics on target; CI: pending | `phase 7: CI, metrics, documentation, publishing` / `phase-7` |
| Ext 1 | Watchdog supervision | not started | | |
| Ext 2 | Fault detection and diagnostics | not started | | |
| Ext 3 | MQTT telemetry | not started | | |
| Ext 4 | Control core on FreeRTOS (v1.1.0) | not started | | |

"Gate result" example: `make test-all: host 42/42, guest 57/57, 0 skipped`.

## Current phase

Phase: 7 (CI, metrics, documentation, publishing): committed locally; next: publish and verify

Phase 7 task checklist:

- [x] `.github/workflows/ci.yml` (jobs `host`, `qemu`; cache; artifacts; free disk)
- [x] `scripts/collect_metrics.py` + `make metrics` (41 metrics); D-026
- [x] `README.md`, `docs/TECHNICAL_REPORT.md` (3,700 words), `THIRD_PARTY.md`, `VERSIONS.md`
- [x] Full gate incl. mutation from a clean build; metrics all on target
- [x] Commit `phase 7: ...`, tag `phase-7`
- [ ] Publish (`gh repo create`), push, `scripts/verify_push.sh`, wait for CI
- [ ] Release `v1.0.0`

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
- Phase 6: host 76 (67 unit + 9 integration), guest 98; fault detection 0.07 s; Modbus round trip 2.15 ms;
  hvacd RSS 1412 kB, binary 26480 bytes; Valgrind 0 errors.
- Phase 7: clean full build 5.9 min (warm caches); 41/41 metrics on target; mutation 3/3 (11/29/4).

## Publishing

- Repository URL: (Phase 7)
- Last pushed commit:
- Tags on remote:
- Last CI run and result:
