# SPDX-License-Identifier: MIT
"""Phase 4: concurrency and lifetime robustness of the driver.

Parallel reads and writes must never return a wrong value or an unexpected error, the module
must survive hundreds of load/unload cycles, and removing the device while a reader is busy must
fail cleanly (an error code), never crash. The kernel log check after each test catches KASAN,
lockdep and other reports.
"""
import errno
import os
import threading

import hvac_test_lib as h

READ_THREADS = 8
READS_PER_THREAD = 1000
MIXED_READERS = 4
MIXED_WRITERS = 2
MIXED_TOTAL_OPS = 2000
STRESS_CYCLES = int(os.environ.get("HVAC_STRESS_CYCLES", "500"))
TIMEOUT_S = 10.0
ALLOWED_REMOVE_ERRNOS = {errno.ENODEV, errno.EIO, errno.ENOENT}


def run_threads(targets: list) -> None:
    threads = [threading.Thread(target=t) for t in targets]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


def test_concurrent_reads_return_the_set_value(hvac_device: int) -> None:
    h.set_temp_raw(400)
    d = h.iio_dir()
    errors: list[object] = []
    lock = threading.Lock()

    def reader() -> None:
        bad = [r for r in (h.read_attr("in_temp_raw", d) for _ in range(READS_PER_THREAD))
               if r != (True, "400")]
        with lock:
            errors.extend(bad)

    run_threads([reader] * READ_THREADS)
    h.record_metric("concurrent_read_errors", len(errors), "count",
                    "Wrong or failed readings with 8 threads reading at once (8 x 1000 reads)")
    assert errors == []


def test_concurrent_reads_and_alarm_writes(hvac_device: int) -> None:
    h.set_temp_raw(-400)
    d = h.iio_dir()
    per_reader = MIXED_TOTAL_OPS // 2 // MIXED_READERS
    per_writer = MIXED_TOTAL_OPS // 2 // MIXED_WRITERS
    problems: list[object] = []
    lock = threading.Lock()
    # Each writer owns one attribute, so "the last written value" is well defined.
    last = {"alarm_low_millicelsius": 0, "alarm_high_millicelsius": 0}

    def reader() -> None:
        for _ in range(per_reader):
            r = h.read_attr("in_temp_raw", d)
            if r != (True, "-400"):
                with lock:
                    problems.append(("read", r))

    def writer(attr: str, base: int) -> None:
        for i in range(per_writer):
            value = base + (i % 100) * 1000    # whole degrees: exact after rounding
            r = h.write_attr(attr, str(value), d)
            if r != (True, None):
                with lock:
                    problems.append(("write", attr, r))
            last[attr] = value

    run_threads([reader] * MIXED_READERS +
                [lambda: writer("alarm_low_millicelsius", -30000),
                 lambda: writer("alarm_high_millicelsius", 20000)])
    assert problems == []
    for attr, value in last.items():
        assert h.read_attr(attr, d) == (True, str(value))


def test_load_unload_stress(stub: int) -> None:
    h.set_temp_raw(400)
    for cycle in range(STRESS_CYCLES):
        h.run(["modprobe", "hvac_temp"])
        h.new_device(h.CHIP_ADDR)
        assert h.wait_until(lambda: h.iio_dir() is not None, TIMEOUT_S), f"cycle {cycle}"
        assert h.read_attr("in_temp_raw") == (True, "400"), f"cycle {cycle}"
        h.delete_device(h.CHIP_ADDR)
        h.run(["modprobe", "-r", "hvac_temp"])
    h.record_metric("driver_load_unload_cycles", STRESS_CYCLES, "count",
                    "Times the driver was loaded, used and removed without a problem")


def test_remove_while_reading_fails_cleanly(hvac_device: int) -> None:
    h.set_temp_raw(400)
    d = h.iio_dir()
    stop = threading.Event()
    seen: list[tuple[bool, object]] = []

    def reader() -> None:
        while not stop.is_set():
            seen.append(h.read_attr("in_temp_raw", d))

    t = threading.Thread(target=reader)
    t.start()
    try:
        assert h.wait_until(lambda: len(seen) >= 20, TIMEOUT_S)
        h.delete_device(h.CHIP_ADDR)
        assert h.wait_until(lambda: not seen[-1][0], TIMEOUT_S)
    finally:
        stop.set()
        t.join()
    unexpected = [r for r in seen
                  if r != (True, "400") and not (not r[0] and r[1] in ALLOWED_REMOVE_ERRNOS)]
    assert unexpected == []
    h.new_device(h.CHIP_ADDR)
    assert h.wait_until(lambda: h.iio_dir() is not None, TIMEOUT_S)
    assert h.read_attr("in_temp_raw") == (True, "400")
