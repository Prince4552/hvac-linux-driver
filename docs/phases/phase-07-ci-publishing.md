# Phase 7: Continuous integration, metrics, documentation, publishing

## 1. What this phase adds

The project becomes public and self-checking:
- A GitHub Actions workflow builds and tests every push.
- `make metrics` gathers every number into one table with targets.
- The README and the technical report explain the whole system.
- The repository is published as release v1.0.0, and a script proves the push really landed.

## 2. Why it matters

In a team, "it passed on my laptop" is not enough. Continuous integration (CI) runs the same checks
on a clean machine for every change, so a broken build is noticed within the hour, by everyone. A
metrics table with targets turns "seems fine" into numbers that a reviewer, a manager or a customer can
check.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `.github/workflows/ci.yml` | Two CI jobs: `host` (make test-host) and `qemu` (make test-qemu) |
| `scripts/collect_metrics.py` | Builds `results/metrics.json` and `results/METRICS.md` with targets |
| `scripts/build.sh` | Also records the duration of the last full build |
| `scripts/verify_push.sh` | Checks that local and remote branches and tags match |
| `README.md` | Front page: what, why, quick start, results, map |
| `docs/TECHNICAL_REPORT.md` | Plain-language report of the whole project |
| `THIRD_PARTY.md`, `VERSIONS.md` | Every external component with exact version and license |

## 4. How it works

```
 git push --> GitHub Actions
                job host (ubuntu-24.04):  apt packages -> venv -> make test-host -> upload results/host
                job qemu (needs host):    free disk -> restore download/ccache cache -> make test-qemu
                                          -> upload results/ (logs included, even on failure)
 locally:      make test-all && make mutation && make metrics   (the full Phase 7 gate)
               git push, then scripts/verify_push.sh, then gh run watch
```

1. **Caching.** Buildroot downloads about 1 GB of sources, and ccache (a compiler cache) remembers
   compiled files. The CI cache keeps both between runs. The key is a hash of `VERSIONS.md`, the
   defconfig and the kernel fragment, so changing a version starts a fresh cache.
2. **Artifacts.** CI uploads `results/` (including logs) even when a job fails, so a failure can be
   investigated without re-running it.
3. **Free disk space.** GitHub's runners come with about 14 GB free. The QEMU job first deletes
   preinstalled toolchains it does not need (.NET, Android, GHC).
4. **Metrics.** Every test step writes a small JSON file. `collect_metrics.py` merges them, compares each
   number with its target, and writes a Markdown table with a plain-English meaning for every row.
5. **Push verification.** After pushing, `verify_push.sh` checks that the local and remote `main` are the
   same commit with the same history length, and that every tag points at the same commit on both sides.

## 5. Run it yourself

```
make metrics && less results/METRICS.md
scripts/verify_push.sh          # after a push
gh run list --limit 3           # recent CI runs
```

## 6. Tests and results

Gate `make test-all && make mutation && make metrics` on 2026-11-15, starting from an empty Buildroot
output folder (clean build 5.9 min with warm download cache and ccache):

| Check | Result |
| --- | --- |
| Host tests | 76 passed, 0 failed |
| Guest tests | 98 passed, 0 failed, 0 skipped |
| Static analysis | checkpatch 0/0, sparse 0, cppcheck 0 |
| Mutation | 3/3 killed (11, 29, 4 failing tests) |
| Metrics | 41 metrics, all meet their target ([results/METRICS.md](../../results/METRICS.md)) |
| CI on the pushed commit | see PROGRESS.md (Publishing) |

## 7. Key concepts

- **Continuous integration.** Every change is built and tested automatically on a clean machine.
- **Reproducibility.** Pinned versions plus a clean machine plus one command means anyone gets the same
  result.
- **Artifacts.** Files a CI job keeps after it ends (logs, results).
- **Reading the metrics.** Every row has a target. "report" rows are facts with no pass/fail line, such as
  sizes and emulated timings, which are only meaningful relative to each other.

## 8. What could go wrong

- The CI tool versions (cppcheck, GCC) differ from the development host, so a new warning can appear in
  CI only. Fix it in the code, never by switching the check off.
- The first CI build of the image is slow (no cache yet). The QEMU job allows 300 minutes.
- Committing large files breaks the 5 MB rule. The pre-push check lists the largest objects.

## 9. Interview questions

1. *What does your CI do?* Two jobs on every push. One runs host tests (unit tests under sanitizers and
   Valgrind, coverage, simulation, daemon integration, lint). The other builds the ARM64 image and runs
   98 tests inside QEMU. Results and logs are uploaded as artifacts.
2. *Why is mutation testing not in CI?* It rebuilds the image three times (about 20 minutes locally), so
   it runs at release gates instead.
3. *How do you know a push succeeded?* A script compares local and remote heads, commit counts and every
   tag, and then the CI run for that exact commit is watched until it finishes.
4. *How would you speed CI up?* Cache downloads and ccache (done), split guest tests across parallel
   QEMU instances, or use a runner with hardware virtualization (KVM).
