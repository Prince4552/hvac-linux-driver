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
| 7 | CI, metrics, documentation, publishing (v1.0.0) | done | `make test-all && make mutation && make metrics`: host 76/76, guest 98/98, 0 skipped, mutation 3/3, 41/41 metrics on target; CI run 43760149090: host + qemu success | `phase 7: CI, metrics, documentation, publishing` / `phase-7` |
| Ext 1 | Watchdog supervision | done | `make test-all`: host 81/81, guest 101/101, 0 skipped | `ext 1: Watchdog supervision` / `ext-1` |
| Ext 2 | Fault detection and diagnostics | done | `make test-all`: host 93/93 (82 unit x3 + 11 integration), guest 101/101, 0 skipped; 45/45 metrics | `ext 2: Fault detection and diagnostics` / `ext-2` |
| Ext 3 | MQTT telemetry | done | `make test-all`: host 107/107 (91 unit x3 + 16 integration), guest 106/106, 0 skipped; 48/48 metrics | `ext 3: MQTT telemetry` / `ext-3` |
| Ext 4 | Control core on FreeRTOS (v1.1.0) | not started | | |

"Gate result" example: `make test-all: host 42/42, guest 57/57, 0 skipped`.

## Current phase

Phase: Ext 3 (MQTT telemetry)

Ext 3 task checklist:

- [x] D-029 (file split, payload, connection mechanics, loop-period measurement, metrics)
- [x] `telemetry_json.c` (pure formatter) + `telemetry_mqtt.c` (non-blocking libmosquitto client)
- [x] Topics, QoS 1, retained online, last-will offline; config keys with validation
- [x] Buildroot: mosquitto (broker, clients, library); board config sends to the local broker
- [x] Unit tests (formatter, config); host tests (broker on 18830, Valgrind with MQTT); guest tests
- [x] Bug found and fixed: `mb_mc_to_cc()` overflow at int32 limits (regression test added)
- [x] Gate `make test-all`, `make metrics` (`mqtt_messages_received_ratio`, `mqtt_reconnect_s`,
      `control_loop_jitter_broker_down_ms`)
- [x] Learning note results, README, TECHNICAL_REPORT section 8.3
- [ ] Commit `ext 3: ...`, tag `ext-3`, push, verify, CI

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
- Ext 1: watchdog trigger latency 1.94 s (timeout 2 s); host 81, guest 101.
- Ext 2: FDD detection 5/5 (100 %), mean time to detect 26.4 min, false alarms 0/15; hvac_fdd.c 100 %
  lines / 93.5 % branches; host 93, guest 101; 45/45 metrics on target.
- Ext 3: MQTT received ratio 1.0; control-loop jitter with broker down 4 ms; reconnect 4.78 s;
  telemetry_json.c 96.2 % lines / 94.4 % branches; host 107, guest 106; 48/48 metrics on target.

## Publishing

- Repository URL: https://github.com/Prince4552/hvac-linux-driver
- Release: `v1.0.0` (at `phase-7`), https://github.com/Prince4552/hvac-linux-driver/releases/tag/v1.0.0
- Last pushed commit: `e946bc4` (ext 2), verified with `scripts/verify_push.sh`
- Tags on remote: phase-0 .. phase-7, ext-1, ext-2, v1.0.0
- Last CI run and result: `phase 7` commit, run 43760149090, host + qemu success; `ext 1` runs 43799393231 (main) and 43799401402 (tag): success; `ext 2` run 43800200090: pending
