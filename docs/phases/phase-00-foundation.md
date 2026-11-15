# Phase 0: Foundation

## 1. What this phase adds

A clean repository skeleton with the folder layout for the whole project, a script that checks the
development machine, a pinned Python tool environment, a lint script, and a top-level `Makefile`.
No product code yet: this phase sets the rules of the road.

## 2. Why it matters

Real embedded teams (for example at an HVAC manufacturer) spend real effort on the "boring" parts:
reproducible builds, pinned tool versions and one-command checks. Without them, "it works on my
machine" bugs eat days. A new team member should be able to run one command and know whether their
machine is ready.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `.gitignore` | Keeps build outputs, downloads, caches and logs out of git |
| `LICENSE` | MIT for everything, except `driver/` which is GPL-2.0-only |
| `THIRD_PARTY.md` | Every external component, its version and license |
| `VERSIONS.md` | Exact pinned versions and checksums |
| `requirements-host.txt` | Exact versions of the host Python tools and all their dependencies |
| `Makefile` | Short, memorable commands (`make lint`, `make check-env`, ...) |
| `scripts/check_env.sh` | Checks tools, libraries, disk, RAM, git identity and GitHub login |
| `scripts/setup_host_venv.sh` | Creates `.venv` (Python virtual environment) from the pinned list |
| `scripts/lint.sh` | Runs `shellcheck` on shell scripts and `pyflakes` on Python files |
| `*/README.md` | Short note in each top-level folder saying what will go there |

## 4. How it works

```
make check-env ---> scripts/check_env.sh ---> PASS/FAIL table, apt command for anything missing
make venv      ---> scripts/setup_host_venv.sh ---> .venv/ with exact tool versions
make lint      ---> scripts/lint.sh ---> shellcheck + pyflakes ---> results/host/lint.json
```

1. `check_env.sh` loops over a list of `command:apt-package` pairs. If a command is missing it remembers
   the package, and at the end prints one `sudo apt install ...` line you can copy. It never runs sudo
   itself: installing system software is the owner's decision.
2. `setup_host_venv.sh` stores a checksum of `requirements-host.txt` inside `.venv`. If the file has not
   changed, it does nothing (safe to re-run, "idempotent").
3. `lint.sh` asks git for the file list (so ignored folders like `.venv/` are skipped automatically),
   picks shell and Python files, and runs the two checkers.
4. Make targets that belong to later phases exist but fail on purpose with "added in Phase N", so a
   placeholder can never be mistaken for a passing step.

## 5. Run it yourself

```
make check-env     # expect: "check-env: all checks passed"
make venv          # expect: "[venv] done" the first time, "[venv] up to date" afterwards
make lint          # expect: "[lint] clean"
make build         # expect: "make build: added in Phase 1" and exit code 1 (for now)
```

## 6. Tests and results

| Check | What it checks | Result |
| --- | --- | --- |
| `make check-env` | 29 commands, 7 packages, disk, RAM, git identity, `gh` login | all pass |
| `make venv` | Pinned tools install cleanly | pass |
| `make lint` | 3 shell scripts with shellcheck, 0 Python files with pyflakes | clean |

## 7. Key concepts

- **Repository structure.** Each folder has one job (driver, thermostat, tests, build config). A reader
  can guess where something lives without searching.
- **Pinned versions.** "Version 9.2.0" means the same code forever; "latest" means whatever was released
  last night. Pinning makes a build reproducible: the same inputs always give the same outputs.
- **Makefile target.** A named recipe (`make lint`). `make` runs the commands listed under the name.
  Here each target only calls one script, so the logic stays in readable shell files.
- **Git tags.** A permanent, human-readable name for one commit (`phase-0`). Later you can run
  `git checkout phase-0` to see the project exactly as it was at the end of this phase.

## 8. What could go wrong

- Working under `/mnt/c` (the Windows drive) makes builds very slow and breaks file permissions; the
  check refuses that location.
- Windows copies files with hidden `:Zone.Identifier` companions; they are git-ignored.
- An unpinned Python dependency can change behavior silently; every package is pinned (decision D-004).

## 9. Interview questions

1. *Why pin tool versions?* So a build is reproducible: anyone, any day, gets the same result, and a
   change in behavior is always caused by a change you made on purpose.
2. *What is a virtual environment in Python?* A private folder of packages for one project, so it does
   not clash with system packages or other projects.
3. *Why do placeholder targets fail instead of succeed?* A silent success would look like a passing test
   that tested nothing. Failing loudly is safer.
4. *What does a linter do?* It reads code without running it and reports likely mistakes, such as an
   unquoted shell variable that breaks on a file name with a space.
