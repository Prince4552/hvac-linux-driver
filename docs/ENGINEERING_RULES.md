# Engineering rules

These rules apply to every file in the repository. They exist to keep the code easy to read for a
beginner, to prevent bugs, and to make memory handling safe. When a rule and convenience conflict,
follow the rule.

## 1. Simplicity and modularity

- One responsibility per file. The file's top comment says what that responsibility is and how the file
  fits into the system (two to five lines).
- Size limits: a C file stays under 400 lines; a function under 50 lines (kernel functions under 60);
  nesting at most 3 levels deep. Split before you exceed them.
- Prefer plain code over clever code: no function-like macros where a `static inline` function works,
  no token pasting, no deep pointer tricks, no premature abstraction or "framework" layers.
- No copy-paste. Code needed in two places lives in one place and is shared (for example the conversion
  header used by both the driver and its KUnit tests, and `hvac_core.c` used by the daemon, the
  simulation and the FreeRTOS port).
- Names say what things are. Every number with a meaning is a named constant whose name carries its unit.

## 2. Units and naming conventions

| Quantity | Type | Unit | Name suffix | Example |
| --- | --- | --- | --- | --- |
| Temperature (internal) | `int32_t` / `int` | millidegrees Celsius | `_mc` | `cool_setpoint_mc` |
| Temperature (Modbus) | `int16_t` | centidegrees Celsius | `_cc` | `temp_cc` |
| Time (user space, core) | `uint64_t` | milliseconds, monotonic | `_ms` | `now_ms` |
| Durations in config | integer | seconds | `_s` | `min_off_s` |
| Raw sensor value | `int` | 1/16 °C steps (0.0625 °C) | `raw` | `raw` |
| Register value | `u16` / `uint16_t` | chip register bits | `reg` | `reg` |

- C: `snake_case` for functions and variables, `UPPER_CASE` for constants and enum values,
  module prefix on every public symbol (`hvac_temp_`, `hvac_core_`, `sensor_iio_`, `mb_`, ...).
- Never use floating point in kernel code. User-space control code also uses integers only, so results
  are exact and identical on every machine. Floating point is allowed only in Python analysis scripts.

## 3. Kernel memory and safety rules (driver)

1. Allocate and register with `devm_*` helpers (`devm_iio_device_alloc`, `devm_iio_device_register`,
   `devm_mutex_init` if the pinned kernel has it). Resources are then freed automatically, in the right
   order, when the device goes away. Avoid a hand-written `remove()` unless something cannot use `devm_`.
2. No global mutable state. Per-device state lives in the private struct from `iio_priv()`. Tables are `const`.
3. Check every return value. Pass negative error codes (`-EIO`, `-EINVAL`, ...) upward unchanged.
   Use `dev_err_probe()` for probe failures.
4. Validate every value that comes from user space before using it: parse with `kstrtoint()`, check the
   range, return `-EINVAL` on bad input. Never trust lengths or contents of user buffers.
5. Write sysfs output only with `sysfs_emit()` (bounds-safe). Never use `sprintf()` into a user buffer.
6. No variable-length arrays and no large stack buffers (keep stack use under 256 bytes per function).
7. Locking: one mutex per device, taken around each complete I2C conversation, never held while
   returning to user space. Never use a spinlock around I2C (I2C transfers may sleep).
8. The module must load and unload any number of times with zero kmemleak reports, zero KASAN reports
   and zero lockdep warnings. The test suite proves this (Phase 4).
9. Kernel code follows the Linux coding style (tabs, 100-column limit) and passes `checkpatch.pl` with
   no errors and no warnings except those explicitly allowed in `scripts/static_analysis.sh` (each allowed
   warning type is listed with its reason in `docs/DECISIONS.md`).
10. Public functions get kernel-doc comments (`/** ... */`).

## 4. User-space memory and safety rules (C daemon, simulation, FreeRTOS port)

1. Language standard C11. Compile with
   `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Wsign-conversion -Werror`.
   Cast explicitly and only where the value is known to fit, with a comment if not obvious.
   Vendored third-party code (Unity) is compiled as a separate target with its own default flags and
   without `-Werror`, and is excluded from `cppcheck` and coverage.
2. The control core (`hvac_core.c`, `hvac_fdd.c`) never allocates memory, never does input/output, never
   reads a clock. It is a pure function of its inputs. This makes it testable and portable.
3. The daemon allocates only during start-up (for example the libmodbus context) and frees everything on
   exit. No allocation inside the control loop. Valgrind must report zero errors and zero definite or
   indirect leaks.
4. Fixed-size buffers only, with the size passed alongside. Use `snprintf()` and check its return value
   for errors and truncation.
5. Banned functions: `gets`, `strcpy`, `strcat`, `sprintf`, `vsprintf`, `atoi`, `atol`, `atof`, `strtok`.
   Parse numbers with `strtol()`/`strtoll()` and check `errno`, the end pointer and the range.
6. Every function that can fail returns a status (`0` on success, negative `errno`-style value on error)
   and its caller checks it. No ignored return values.
7. Initialize every variable at declaration. Zero-initialize structs (`= {0}`).
8. Signal handlers only set a `volatile sig_atomic_t` flag. All cleanup happens in normal code.
9. The daemon is single-threaded (one `select()` loop). No threads, so no data races.
10. Unit tests run three ways: normal build, AddressSanitizer + UndefinedBehaviorSanitizer build, and
    under Valgrind memcheck. All three must be clean.

## 5. Python rules (tests and tools)

- Python 3, standard library first. Host-only extras are pinned in `requirements-host.txt`
  (pytest, pyflakes, gcovr, matplotlib). Guest tests may use only the standard library and pytest.
- Type hints on public functions. Small, plainly named functions. No metaprogramming.
- Must pass `python3 -m pyflakes` on the host.

## 6. Shell rules

- Every script starts with `#!/usr/bin/env bash` (or `#!/bin/sh` inside the guest) and `set -euo pipefail`
  (`set -eu` for BusyBox `sh`).
- Quote every variable. Pass `shellcheck` with zero findings.
- Scripts print each major step, are safe to re-run (idempotent), and exit non-zero on any failure.
- Scripts that change state clean up with `trap` (for example the mutation-test script always restores the
  original driver source).

## 7. Testing rules

1. Every behavior in the plan has at least one test. Every bug you fix gets a regression test first.
2. Tests are deterministic: fixed random seeds, no fixed `sleep()` waits. Use a polling helper
   (`wait_until(condition, timeout_s)`) with a bounded timeout.
3. Tests are independent: each sets up and tears down its own state through fixtures, and teardown runs
   even when the test fails.
4. Test names describe the behavior: `test_negative_temperature_is_sign_extended`.
5. After every in-guest test, the kernel log since the test started is checked for `BUG:`, `WARNING:`,
   `Oops`, `Call Trace`, `KASAN`, `kmemleak`, `possible circular locking`, `INFO: task`. Any match fails the test.
6. No network access in tests except to `localhost`.
7. A phase gate passes only with 0 failed, 0 errors, 0 skipped. Never weaken a test to pass the gate.

## 8. Git rules

- Branch `main`. Small commits inside a phase are fine; every phase ends with one commit named
  `phase N: <title>` (or `ext N: <title>`) and an annotated tag `phase-N` (or `ext-N`).
- Never commit at a phase boundary unless the gate passed. Never force-push. Never rewrite pushed history.
- `.gitignore` covers `.cache/`, `build*/`, `results/logs/`, `results/tmp/`, `.venv/`, `__pycache__/`,
  `*.o`, `*.ko`, `*.mod*`, `*.cmd`, `Module.symvers`, `modules.order`, coverage output.
- Commit `results/metrics.json`, `results/METRICS.md` and small figures in `docs/images/` (under 1 MB each).

## 9. Licensing and third-party code

- `driver/` is `GPL-2.0-only` (a Linux kernel module must be GPL-compatible to use IIO symbols).
  Everything else is MIT. Every source file starts with an SPDX line. `LICENSE` explains the split.
- Third-party code is never copied into the repo, except Unity (the small C test framework) vendored
  under `thermostat/third_party/unity/` with its license. List all third-party components, versions and
  licenses in `THIRD_PARTY.md`.

## 10. Documentation rules

- Documentation changes go in the same commit as the code they describe.
- Write for a reader with no embedded background: expand acronyms, explain why, show the command and the
  expected output.
- Never claim something works without a test or a measured metric that shows it.
