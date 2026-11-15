# SPDX-License-Identifier: MIT
"""Shared helpers for the in-guest tests (PROJECT_PLAN.md section 6.2).

Every guest test uses these instead of talking to the kernel directly, so details such as the
i2c-stub byte order (section 4.1) and "find devices by name, never by number" live in one place.
Standard library only: this file runs inside the QEMU guest.
"""
import json
import os
import re
import subprocess
import time
import uuid
from typing import Callable, Optional

CHIP_ADDR = 0x48
STUB_ADAPTER_NAME = "SMBus stub driver"
DRIVER_NAME = "hvac_temp"
# Adapters appear as i2c-N here (the old /sys/class/i2c-adapter is gone in Linux 6.18).
I2C_DEVICES = "/sys/bus/i2c/devices"
IIO_DEVICES = "/sys/bus/iio/devices"
RESULTS_DIR = os.environ.get("HVAC_RESULTS_DIR", "/tmp/hvac-results")

# Kernel log patterns that mean something went wrong (ENGINEERING_RULES.md section 7.5).
BAD_LOG_PATTERNS = ("BUG:", "WARNING:", "Oops", "Call Trace", "KASAN", "kmemleak",
                    "possible circular locking", "INFO: task")
MARKER_PREFIX = "hvac-test-marker"


class CommandError(RuntimeError):
    """A command exited with a non-zero status."""


def run(cmd: list[str], check: bool = True) -> str:
    """Runs a command and returns its stdout; raises CommandError with stderr on failure."""
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if check and proc.returncode != 0:
        raise CommandError(f"{' '.join(cmd)} -> exit {proc.returncode}: {proc.stderr.strip()}")
    return proc.stdout


def module_loaded(name: str) -> bool:
    with open("/proc/modules", encoding="utf-8") as f:
        return any(line.split()[0] == name for line in f)


# ---------------------------------------------------------------- simulated chip (i2c-stub)

def load_stub(addr: int = CHIP_ADDR) -> int:
    """Loads i2c-stub with one simulated chip at addr; returns the stub's I2C bus number."""
    run(["modprobe", "i2c-stub", f"chip_addr=0x{addr:02x}"])
    bus = stub_bus()
    if bus is None:
        raise RuntimeError("i2c-stub loaded but its adapter was not found")
    return bus


def unload_stub() -> None:
    if module_loaded("i2c_stub"):
        run(["modprobe", "-r", "i2c-stub"])


def stub_bus() -> Optional[int]:
    """Returns the bus number of the adapter named 'SMBus stub driver', or None."""
    if not os.path.isdir(I2C_DEVICES):
        return None
    for entry in sorted(os.listdir(I2C_DEVICES)):
        if not re.fullmatch(r"i2c-\d+", entry):
            continue  # client devices such as 1-0048 live here too
        name_file = os.path.join(I2C_DEVICES, entry, "name")
        try:
            with open(name_file, encoding="utf-8") as f:
                if f.read().strip() == STUB_ADAPTER_NAME:
                    return int(entry.split("-")[1])
        except OSError:
            continue
    return None


def swap16(value: int) -> int:
    """Swaps the two bytes of a 16-bit value (chip order <-> SMBus order used by i2c-stub)."""
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF)


def _require_bus() -> int:
    bus = stub_bus()
    if bus is None:
        raise RuntimeError("i2c-stub is not loaded")
    return bus


def set_chip_reg(reg: int, value16: int, addr: int = CHIP_ADDR) -> None:
    """Stores a chip register value (most significant byte first, as the chip sends it)."""
    # -f (force): talk to the address even while hvac_temp has claimed it. Safe here because
    # i2c-stub is only memory; on real hardware this could confuse a running driver.
    run(["i2cset", "-f", "-y", str(_require_bus()), f"0x{addr:02x}", f"0x{reg:02x}",
         f"0x{swap16(value16):04x}", "w"])


def get_chip_reg(reg: int, addr: int = CHIP_ADDR) -> int:
    """Returns a chip register value (most significant byte first, as the chip sends it)."""
    out = run(["i2cget", "-f", "-y", str(_require_bus()), f"0x{addr:02x}", f"0x{reg:02x}", "w"])
    return swap16(int(out.strip(), 16))


def raw_to_reg(raw: int) -> int:
    """Python mirror of hvac_temp_raw_to_reg() in driver/hvac_temp_conv.h."""
    return (raw & 0xFFF) << 4


def set_temp_raw(raw: int) -> None:
    """Makes the simulated chip report the given raw temperature (1 step = 0.0625 C)."""
    set_chip_reg(0x00, raw_to_reg(raw))


# ---------------------------------------------------------------- driver device and IIO files

def _bus_dir(bus: int) -> str:
    return f"/sys/bus/i2c/devices/i2c-{bus}"


def new_device(addr: int = CHIP_ADDR, name: str = DRIVER_NAME) -> None:
    """Asks the I2C core to create a device at addr on the stub bus (this triggers probe)."""
    with open(os.path.join(_bus_dir(_require_bus()), "new_device"), "w", encoding="utf-8") as f:
        f.write(f"{name} 0x{addr:02x}")


def delete_device(addr: int = CHIP_ADDR) -> None:
    with open(os.path.join(_bus_dir(_require_bus()), "delete_device"), "w",
              encoding="utf-8") as f:
        f.write(f"0x{addr:02x}")


def client_dir(addr: int = CHIP_ADDR) -> str:
    """sysfs folder of the I2C client device, for example /sys/bus/i2c/devices/1-0048."""
    return f"/sys/bus/i2c/devices/{_require_bus()}-{addr:04x}"


def bound_driver(addr: int = CHIP_ADDR) -> Optional[str]:
    """Name of the driver bound to the I2C client at addr, or None if unbound or absent."""
    link = os.path.join(client_dir(addr), "driver")
    return os.path.basename(os.readlink(link)) if os.path.islink(link) else None


def iio_dir(name: str = DRIVER_NAME) -> Optional[str]:
    """Finds /sys/bus/iio/devices/iio:deviceN whose 'name' file matches; None if absent."""
    if not os.path.isdir(IIO_DEVICES):
        return None
    for entry in sorted(os.listdir(IIO_DEVICES)):
        path = os.path.join(IIO_DEVICES, entry)
        try:
            with open(os.path.join(path, "name"), encoding="utf-8") as f:
                if f.read().strip() == name:
                    return path
        except OSError:
            continue
    return None


def read_attr(name: str, device_dir: Optional[str] = None) -> tuple[bool, object]:
    """Reads one sysfs attribute of our IIO device: (True, text) or (False, errno)."""
    path = os.path.join(device_dir or iio_dir() or "/nonexistent", name)
    try:
        fd = os.open(path, os.O_RDONLY)
    except OSError as e:
        return False, e.errno
    try:
        return True, os.read(fd, 4096).decode().strip()
    except OSError as e:
        return False, e.errno
    finally:
        os.close(fd)


def write_attr(name: str, value: str, device_dir: Optional[str] = None) -> tuple[bool, object]:
    """Writes one sysfs attribute in a single write() call: (True, None) or (False, errno)."""
    path = os.path.join(device_dir or iio_dir() or "/nonexistent", name)
    try:
        fd = os.open(path, os.O_WRONLY)
    except OSError as e:
        return False, e.errno
    try:
        os.write(fd, value.encode())
        return True, None
    except OSError as e:
        return False, e.errno
    finally:
        os.close(fd)


# ---------------------------------------------------------------- kernel log

def kernel_log_marker() -> str:
    """Writes a unique line to the kernel log and returns it."""
    marker = f"{MARKER_PREFIX} {uuid.uuid4().hex}"
    with open("/dev/kmsg", "w", encoding="utf-8") as f:
        f.write(marker + "\n")
    return marker


def kernel_log() -> list[str]:
    return run(["dmesg"]).splitlines()


def kernel_log_since(marker: str) -> list[str]:
    """Kernel log lines written after the marker. Fails if the marker was lost (log overflow)."""
    lines = kernel_log()
    for i, line in enumerate(lines):
        if line.endswith(marker):
            return lines[i + 1:]
    raise AssertionError(f"kernel log marker '{marker}' not found (log buffer overflowed?)")


def bad_log_lines(lines: list[str], allowed: tuple[str, ...] = ()) -> list[str]:
    """Lines matching a bad pattern and none of the allowed regular expressions."""
    found = []
    for line in lines:
        if any(p in line for p in BAD_LOG_PATTERNS) and \
                not any(re.search(a, line) for a in allowed):
            found.append(line)
    return found


def assert_kernel_log_clean(lines: list[str], allowed: tuple[str, ...] = ()) -> None:
    bad = bad_log_lines(lines, allowed)
    assert not bad, "kernel log shows problems:\n" + "\n".join(bad[:40])


# ---------------------------------------------------------------- polling and metrics

def wait_until(predicate: Callable[[], bool], timeout_s: float,
               interval_s: float = 0.05) -> bool:
    """Polls predicate until it is true (returns True) or timeout_s passes (returns False)."""
    deadline = time.monotonic() + timeout_s
    while True:
        if predicate():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval_s)


def record_metric(name: str, value: object, unit: str, meaning: str) -> None:
    """Saves one metric as /tmp/hvac-results/metrics/<name>.json for the result summary."""
    metrics_dir = os.path.join(RESULTS_DIR, "metrics")
    os.makedirs(metrics_dir, exist_ok=True)
    with open(os.path.join(metrics_dir, f"{name}.json"), "w", encoding="utf-8") as f:
        json.dump({"name": name, "value": value, "unit": unit, "meaning": meaning}, f)


def record_boot_metric() -> None:
    """Called by the runner before pytest: seconds from kernel start to the start of the tests."""
    with open("/proc/uptime", encoding="utf-8") as f:
        uptime_s = float(f.read().split()[0])
    record_metric("guest_boot_to_tests_s", round(uptime_s, 1), "s",
                  "Seconds from kernel start to the start of the guest tests (emulated board)")
