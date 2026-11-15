# SPDX-License-Identifier: MIT
"""Phase 2 tests: the hello module, and hvac_temp binding (probe) to the simulated chip.

A device at 0x48 has a simulated chip behind it, so probe must succeed. A device at 0x49 has no
chip, so probe must fail cleanly with "chip not responding" and leave nothing behind.
"""
import os

import hvac_test_lib as h

PROBE_TIMEOUT_S = 10.0
NO_CHIP_ADDR = 0x49
BIND_CYCLES = 50


def log_contains(marker: str, text: str) -> bool:
    return any(text in line for line in h.kernel_log_since(marker))


def test_hello_module_logs_load_and_unload() -> None:
    marker = h.kernel_log_marker()
    h.run(["modprobe", "hello"])
    try:
        assert h.wait_until(lambda: log_contains(marker, "hello: loaded"), PROBE_TIMEOUT_S)
    finally:
        h.run(["modprobe", "-r", "hello"])
    assert h.wait_until(lambda: log_contains(marker, "hello: unloaded"), PROBE_TIMEOUT_S)
    assert not h.module_loaded("hello")


def test_driver_module_loads_and_is_listed() -> None:
    h.run(["modprobe", "hvac_temp"])
    try:
        assert h.module_loaded("hvac_temp")
    finally:
        h.run(["modprobe", "-r", "hvac_temp"])
    assert not h.module_loaded("hvac_temp")


def test_probe_succeeds_when_chip_answers(hvac_device: int) -> None:
    assert h.bound_driver(h.CHIP_ADDR) == "hvac_temp"
    assert any("ready at address 0x48" in line for line in h.kernel_log())


def test_probe_fails_cleanly_when_no_chip(stub: int) -> None:
    h.run(["modprobe", "hvac_temp"])
    marker = h.kernel_log_marker()
    try:
        h.new_device(NO_CHIP_ADDR)
        assert h.wait_until(lambda: log_contains(marker, "chip not responding"), PROBE_TIMEOUT_S)
        assert os.path.isdir(h.client_dir(NO_CHIP_ADDR))  # the device exists ...
        assert h.bound_driver(NO_CHIP_ADDR) is None          # ... but no driver is bound
        assert h.iio_dir() is None
    finally:
        h.delete_device(NO_CHIP_ADDR)
        h.run(["modprobe", "-r", "hvac_temp"])


def test_delete_device_unbinds_cleanly(hvac_device: int) -> None:
    h.delete_device(h.CHIP_ADDR)
    assert h.wait_until(lambda: not os.path.exists(h.client_dir(h.CHIP_ADDR)), PROBE_TIMEOUT_S)
    assert h.iio_dir() is None


def test_repeated_load_bind_unbind_unload_cycles_are_clean(stub: int) -> None:
    for _ in range(BIND_CYCLES):
        h.run(["modprobe", "hvac_temp"])
        h.new_device(h.CHIP_ADDR)
        assert h.wait_until(lambda: h.bound_driver(h.CHIP_ADDR) == "hvac_temp", PROBE_TIMEOUT_S)
        h.delete_device(h.CHIP_ADDR)
        h.run(["modprobe", "-r", "hvac_temp"])
    assert not h.module_loaded("hvac_temp")
