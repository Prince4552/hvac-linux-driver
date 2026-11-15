# SPDX-License-Identifier: MIT
"""Phase 4: memory-safety evidence and read latency.

- kmemleak: after 200 load/use/unload cycles, no leaked memory block mentions hvac_temp.
- KASAN and lockdep are enabled, and the kernel log of the whole run has no report from either.
- Read latency of in_temp_raw (emulated board, includes Python overhead: relative only).
"""
import gzip
import re
import time

import hvac_test_lib as h

KMEMLEAK = "/sys/kernel/debug/kmemleak"
LEAK_CYCLES = 200
LATENCY_READS = 1000
TIMEOUT_S = 10.0
KASAN_RE = re.compile(r"BUG: KASAN")
LOCKDEP_RE = re.compile(r"possible circular locking|possible recursive locking|"
                        r"inconsistent lock state|bad unlock balance|"
                        r"lock held when returning to user space|possible irq lock inversion")


def kmemleak_command(cmd: str) -> None:
    with open(KMEMLEAK, "w", encoding="utf-8") as f:
        f.write(cmd)


def one_cycle() -> None:
    h.run(["modprobe", "hvac_temp"])
    h.new_device(h.CHIP_ADDR)
    assert h.wait_until(lambda: h.iio_dir() is not None, TIMEOUT_S)
    assert h.read_attr("in_temp_raw")[0]
    assert h.write_attr("alarm_low_millicelsius", "10000") == (True, None)
    h.delete_device(h.CHIP_ADDR)
    h.run(["modprobe", "-r", "hvac_temp"])


def leak_reports_mentioning(report: str, word: str) -> int:
    """Each kmemleak entry starts with 'unreferenced object'; count entries naming word."""
    entries = report.split("unreferenced object")[1:]
    return sum(1 for e in entries if word in e)


def test_no_kmemleak_reports_for_driver(stub: int) -> None:
    kmemleak_command("clear")       # forget leaks reported before this test (boot noise)
    h.set_temp_raw(400)
    for _ in range(LEAK_CYCLES):
        one_cycle()
    kmemleak_command("scan")
    kmemleak_command("scan")        # second scan removes transient false positives
    with open(KMEMLEAK, encoding="utf-8") as f:
        report = f.read()
    count = leak_reports_mentioning(report, "hvac_temp")
    h.record_metric("kmemleak_reports", count, "count",
                    "Memory blocks our driver allocated and never freed (after 200 cycles)")
    assert count == 0, report


def test_kasan_and_lockdep_enabled_and_silent() -> None:
    with gzip.open("/proc/config.gz", "rt", encoding="utf-8") as f:
        config = f.read()
    for option in ("CONFIG_KASAN=y", "CONFIG_PROVE_LOCKING=y", "CONFIG_DEBUG_KMEMLEAK=y"):
        assert re.search(rf"^{option}$", config, re.MULTILINE), option
    log = h.kernel_log()
    kasan = sum(1 for line in log if KASAN_RE.search(line))
    lockdep = sum(1 for line in log if LOCKDEP_RE.search(line))
    h.record_metric("kasan_reports", kasan, "count",
                    "Invalid memory accesses (use-after-free, out of bounds) in the whole run")
    h.record_metric("lockdep_reports", lockdep, "count",
                    "Locking mistakes that could deadlock, in the whole run")
    assert kasan == 0 and lockdep == 0


def test_read_latency(hvac_device: int) -> None:
    d = h.iio_dir()
    samples_ns = []
    for _ in range(LATENCY_READS):
        start = time.perf_counter_ns()
        ok, _ = h.read_attr("in_temp_raw", d)
        samples_ns.append(time.perf_counter_ns() - start)
        assert ok
    samples_ns.sort()
    median_us = samples_ns[len(samples_ns) // 2] // 1000
    p99_us = samples_ns[(len(samples_ns) * 99) // 100] // 1000
    meaning = ("Time for one temperature read on the emulated board, including Python "
               "overhead ({}); relative numbers only")
    h.record_metric("driver_read_latency_median_us", median_us, "us", meaning.format("median"))
    h.record_metric("driver_read_latency_p99_us", p99_us, "us", meaning.format("99th percentile"))
    assert median_us > 0
