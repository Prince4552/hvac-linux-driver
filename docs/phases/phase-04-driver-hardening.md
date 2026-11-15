# Phase 4: Driver hardening (robustness, memory safety, static analysis, mutation testing)

## 1. What this phase adds

Evidence, not just claims, that the driver is safe:
- Stress and concurrency tests.
- Memory checks by three kernel debugging tools (KASAN, kmemleak, lockdep).
- Static analysis (checkpatch, sparse).
- Mutation testing, which proves the tests catch real bugs.

## 2. Why it matters

A controller runs for years without a reboot. A leak of a few bytes per sensor read, or a lock bug that
deadlocks once a month, will eventually stop the HVAC unit. Kernel bugs are the worst kind: they can
crash the whole system. Industry practice (and safety standards) asks for this kind of evidence before
code ships.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `tests/guest/test_driver_robustness.py` | Concurrent reads, reads + writes, 500 load/unload cycles, remove while reading |
| `tests/guest/test_memory.py` | kmemleak after 200 cycles, KASAN/lockdep silence, read latency |
| `scripts/static_analysis.sh` | checkpatch.pl + sparse against the pinned kernel; `results/host/static_analysis.json` |
| `tests/mutation/mutants/*.patch` | Three deliberate bugs |
| `scripts/run_mutation.sh` | Applies each mutant, rebuilds, runs the guest tests, expects failure |
| `Makefile` | `test-qemu` now runs static analysis; `make mutation` |

## 4. How it works

```
 KASAN     every memory access is checked against "shadow memory" -> use-after-free, overflow
 kmemleak  scans memory for blocks nobody points to any more    -> leaks
 lockdep   records the order locks are taken in                 -> possible deadlocks
                          |
            any report lands in the kernel log, and every test checks the log afterwards
```

1. **Concurrency.** Eight threads each read `in_temp_raw` 1000 times while the value is fixed. Every
   one of the 8000 reads must return it exactly. A second test mixes 4 readers with 2 alarm writers.
2. **Lifetime.** 500 cycles of load module, create device, read, delete device, unload module. Then one
   thread keeps reading while the device is deleted underneath it. Reads must either succeed or fail
   with an error code (`ENODEV`, `EIO`, `ENOENT`), never crash. After that the device is re-created and
   works.
3. **kmemleak.** Clear old reports, run 200 cycles, scan twice (the second scan removes temporary false
   alarms), then count reports that mention `hvac_temp`.
4. **Static analysis.** `checkpatch.pl` checks the Linux coding style and common mistakes. `sparse` is a
   type checker that knows kernel annotations (for example `__be16` big-endian values, or user-space
   pointers).
5. **Mutation testing.** Make the driver wrong on purpose, then check the tests notice:

| Mutant | Bug | Which tests should fail |
| --- | --- | --- |
| `01-no-sign-extension` | raw = `reg >> 4` (negative temperatures become huge positives) | conversion, alarms |
| `02-no-byte-swap` | plain word read/write (bytes in the wrong order) | conversion, alarms |
| `03-no-range-check` | alarm write accepts any value | invalid-input alarm tests |

No "remove the mutex" mutant: a race only shows up when two threads collide at the same instant. On the
simulated chip, a test could pass thousands of times with the bug present, so the score would depend on
luck (D-020). Lock correctness is checked by lockdep and code review instead.

## 5. Run it yourself

```
make test-qemu                       # includes static analysis and all memory tests (about 6 min)
cat results/host/static_analysis.json
make mutation                        # about 20 min; expect "score 3/3"
cat results/host/mutation.json
```

## 6. Tests and results

Gate `make test-all && make mutation` on 2026-11-15: host lint clean; static analysis clean;
guest **90 passed, 0 failed, 0 skipped**; mutation **3/3 killed**.

| Metric | Value | Target | Meaning |
| --- | --- | --- | --- |
| `concurrent_read_errors` | 0 | 0 | Wrong or failed reads with 8 threads x 1000 reads |
| `driver_load_unload_cycles` | 500 | >= 500 | Load, bind, read, unbind, unload cycles without a problem (132 s) |
| `kmemleak_reports` | 0 | 0 | Leaked blocks mentioning `hvac_temp` after 200 cycles |
| `kasan_reports` | 0 | 0 | Invalid memory accesses |
| `lockdep_reports` | 0 | 0 | Possible deadlocks |
| `checkpatch_errors` / `_warnings` | 0 / 0 | 0 / 0 | Kernel style problems (no allowed types needed, D-019) |
| `sparse_warnings` | 0 | 0 | Kernel type-check problems (3 files checked) |
| `mutation_score` | 3/3 | 3/3 | Mutants killed: sign extension (11 failing tests), byte swap (25), range check (4) |
| `driver_read_latency_median_us` / `_p99_us` | about 1100 / 4000 | report | One read, emulated CPU, with Python overhead (relative only) |

Both static checkers were first shown to work: each reported a deliberately broken file. A silent tool
that checks nothing would look exactly like a clean result.

## 7. Key concepts

- **Race condition.** The result depends on the timing of two threads. A mutex lets only one thread run
  the I2C conversation at a time.
- **Mutex vs spinlock.** A spinlock busy-waits and must not sleep. An I2C transfer sleeps while the bus
  works, so a spinlock around it would be a bug. A mutex puts the waiting thread to sleep instead.
- **Use-after-free.** Using memory after it was given back. KASAN catches it at the faulty access.
  `devm_` freeing in the right order prevents it.
- **Static analysis.** Finding bugs by reading code, without running it. Cheap, and catches whole classes
  of mistakes early.
- **Mutation testing.** It measures the tests, not the code. A test suite that does not fail on a broken
  driver is not protecting anything.

## 8. What could go wrong

- kmemleak reports false positives from boot. Clear first, scan twice, and count only our module.
- Kernel log overflow could hide a report. The log buffer is 4 MB, and a lost marker fails the test (D-009).
- The KASAN/lockdep whole-log scan runs in `test_memory.py`. Tests after it are still covered, because
  every single test checks its own slice of the log.
- Timing numbers come from an emulated CPU with Python overhead. Compare them only with each other.

## 9. Interview questions

1. *How do you prove a driver has no memory leaks?* Run it hundreds of times under kmemleak (and KASAN for
   invalid accesses), and require zero reports that mention the module.
2. *Mutex or spinlock around I2C?* Mutex: I2C transfers sleep, and sleeping with a spinlock held is a bug
   (`CONFIG_DEBUG_ATOMIC_SLEEP` would report it).
3. *What does lockdep do?* It learns the order in which locks are taken and warns when two code paths could
   deadlock, even if the deadlock never actually happened.
4. *What is mutation testing and what did it show here?* Deliberately inserting bugs to check the tests
   catch them. All three mutants were killed (score 3/3).
5. *What happens if the device is removed while a program is reading it?* IIO marks the device gone. Further
   reads get an error code instead of touching freed memory, and the test proves it.
