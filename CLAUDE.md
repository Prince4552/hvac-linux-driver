# CLAUDE.md: HVAC Linux driver project

This file is loaded automatically at the start of every Claude Code session. Follow it exactly.

## Mission

Build, test, document and publish to GitHub, end to end, a small embedded Linux project for an HVAC
(heating, ventilation and air conditioning) temperature sensor:

1. A Linux kernel driver (`hvac_temp`) for a TMP102-style I2C temperature sensor, exposed through IIO.
2. An automated test suite that proves the driver is correct and memory safe, with no real hardware
   (the chip is simulated by the kernel's `i2c-stub` module, inside an ARM64 QEMU virtual machine).
3. A user-space thermostat daemon (`hvacd`) with a pure, unit-tested control core and a Modbus TCP interface.
4. Extensions (watchdog, fault detection, MQTT telemetry, FreeRTOS port) after the core is published.

The owner, Priyanshu, is a biomedical engineering student who is new to embedded Linux. He will study
this repository later to learn the subject and to explain it in internship interviews (Carrier, Hyderabad).
**Readability, correctness and memory safety matter more than features or cleverness.**

## Documents you must follow

@docs/ENGINEERING_RULES.md
@docs/PROGRESS.md

- `docs/PROJECT_PLAN.md` is the full specification: architecture, interfaces, phases, tests, metrics.
  Read sections 1 to 6 once at the start of every session, then the section for the current phase.
- `docs/DECISIONS.md` records every choice the plan does not already make. Add to it; never guess silently.
- Where this plan differs from the earlier "Carrier Internship Prep: Project Guide", this plan wins.

## How every phase runs (the phase loop)

1. Read `docs/PROGRESS.md` and find the first phase that is not `done`.
2. Read that phase in `docs/PROJECT_PLAN.md`. Copy its task list into `docs/PROGRESS.md` as checkboxes.
3. Implement in small steps. After each step, build and run the fastest relevant tests.
4. Run the phase gate: `make test-all` plus any extra gate commands the phase lists.
   The gate passes only with **0 failures, 0 errors, 0 skipped tests** and all static checks clean.
5. Write the learning notes for the phase in `docs/phases/` (template in PROJECT_PLAN.md section 10.3).
6. Update `docs/PROGRESS.md` (status, test counts, key metrics) and `docs/DECISIONS.md` if needed.
7. Commit with message `phase N: <phase title>` (extensions: `ext N: <title>`), then tag `phase-N` (or `ext-N`).
8. From Phase 7 onward, also push and verify the push (PROJECT_PLAN.md section 11).
9. Move straight on to the next phase. Do not wait for the user unless you are blocked.

All earlier tests must still pass at every gate (full regression), not only the new ones.

## Hard rules

- **Never run `sudo`.** If a host package is missing, stop and print the exact `sudo apt install ...`
  command for the user, then wait.
- **Never weaken tests to make them pass.** Do not delete, skip, mark xfail, loosen thresholds, add
  retries, or lengthen timeouts to hide a failure. Fix the root cause. If you believe a plan threshold is
  wrong, write the evidence in `docs/DECISIONS.md` and ask the user.
- **Stuck rule:** after 3 failed attempts at the same problem, stop, write the blocker (what you tried,
  exact error, log path) in `docs/PROGRESS.md`, and ask the user.
- **Never guess.** If the plan is silent, choose the simplest option that satisfies it and record it in
  `docs/DECISIONS.md` before relying on it.
- **Do not change public interfaces** (sysfs names, Modbus register map, config keys, file layout,
  test-result format) without a recorded decision.
- **Pinned versions only.** Every external download uses an exact version or commit, recorded in `VERSIONS.md`.
- **Never commit** build outputs, downloads, caches, logs, secrets or any file larger than 5 MB.
- **Never force-push** and never rewrite history that has been pushed.
- Never edit files under `.cache/` (downloaded Buildroot, FreeRTOS). Customize only through `br2-external/`.

## Environment facts (WSL2 Ubuntu)

- Project root: `/home/priyanshu/projects/linux-driver-project`. Never build under `/mnt/c` (slow, wrong permissions).
- WSL adds Windows folders with spaces to `PATH`, which Buildroot rejects. `scripts/build.sh` must set a
  clean `PATH` itself; never ask the user to change WSL settings.
- The first Buildroot build takes 30 to 90 minutes, and a QEMU test run 5 to 30 minutes. Run such commands
  in the background with output going to a log file under `results/logs/`, then poll the log until they
  finish. Do not stop them early and do not rerun them while one is still running.
- QEMU runs without KVM here (ARM64 emulated on x86), so the guest is slow. Timing metrics are relative only.
- GitHub CLI (`gh`) is already authenticated by the user. Check with `gh auth status`; never print tokens.

## Main commands (created in Phases 0 and 1)

| Command | What it does |
| --- | --- |
| `make check-env` | Verify host tools, disk space, git identity, gh login |
| `make build` | Fetch pinned Buildroot (first time) and build the ARM64 image |
| `make run` | Boot the image interactively (demo mode) |
| `make test-host` | Fast host tests: unit tests, sanitizers, coverage, simulation, static checks |
| `make test-qemu` | Build, boot QEMU in autotest mode, run all in-guest tests, collect results |
| `make test-all` | `test-host` then `test-qemu` (the phase gate) |
| `make metrics` | Regenerate `results/metrics.json` and `results/METRICS.md` |

## GitHub

- Repository name: `hvac-linux-driver`, public, branch `main`.
- Description: "Linux I2C/IIO temperature-sensor driver, hardware-free test suite and HVAC thermostat
  daemon, built with Buildroot and tested in ARM64 QEMU".
- The repository is created in Phase 7 (end of the compulsory part). Until then, commit and tag locally only.

## Writing style for all documentation

Plain English for a reader with no embedded background. Short sentences. Expand every acronym on first
use. Explain why, not only what. Prefer a small table or ASCII diagram over long paragraphs.
