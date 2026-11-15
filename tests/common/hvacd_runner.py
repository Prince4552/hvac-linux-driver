# SPDX-License-Identifier: MIT
"""Starts, queries and stops hvacd for tests; shared by the host and guest test suites.

Standard library only (it also runs inside the QEMU guest).
"""
import signal
import subprocess
import time
from pathlib import Path
from typing import Callable

from modbus_client import ModbusClient

STATE_IDLE, STATE_COOLING, STATE_HEATING, STATE_FAULT = 0, 1, 2, 3
REG_TEMP, REG_STATE, REG_COOL, REG_HEAT, REG_OUT, REG_ALARMS = 0, 1, 2, 3, 4, 5
ALARM_SENSOR_FAULT, ALARM_SETPOINT_REJECTED = 0b01, 0b10

# Short timers so a whole scenario runs in seconds (PROJECT_PLAN.md Phase 6, task 5).
SHORT_TIMER_CONFIG = """\
min_off_s = 2
min_run_s = 2
fault_recover_count = 3
loop_period_ms = {loop_ms}
iio_base_dir = {base}
modbus_port = {port}
log_level = debug
"""


def wait_until(predicate: Callable[[], bool], timeout_s: float, interval_s: float = 0.02) -> bool:
    """Polls predicate until true (returns True) or until timeout_s passes (returns False)."""
    deadline = time.monotonic() + timeout_s
    while True:
        if predicate():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval_s)


def write_short_timer_config(path: Path, base: str, port: int, loop_ms: int) -> Path:
    path.write_text(SHORT_TIMER_CONFIG.format(loop_ms=loop_ms, base=base, port=port))
    return path


class Daemon:
    """One hvacd process; stdout and stderr go to log_path."""

    def __init__(self, cmd: list[str], log_path: Path, port: int) -> None:
        self.log_path = log_path
        self.port = port
        self.log = open(log_path, "w", encoding="utf-8")
        self.proc = subprocess.Popen(cmd, stderr=self.log, stdout=self.log)

    def regs(self) -> list[int]:
        """Reads all 10 holding registers over Modbus TCP."""
        with ModbusClient(port=self.port) as client:
            return client.read_registers(0, 10)

    def client(self) -> ModbusClient:
        return ModbusClient(port=self.port)

    def state_is(self, state: int) -> Callable[[], bool]:
        return lambda: self.regs()[REG_STATE] == state

    def wait_ready(self, timeout_s: float) -> None:
        """Waits until the Modbus port answers; fails early if the process exits."""
        def ready() -> bool:
            if self.proc.poll() is not None:
                raise AssertionError(f"hvacd exited: {self.log_path.read_text()}")
            try:
                self.regs()
                return True
            except OSError:
                return False
        assert wait_until(ready, timeout_s, 0.1), "hvacd did not open its Modbus port"

    def stop(self, timeout_s: float) -> int:
        """Sends SIGTERM and returns the exit code."""
        self.proc.send_signal(signal.SIGTERM)
        try:
            return self.proc.wait(timeout=timeout_s)
        finally:
            self.log.close()

    def kill(self) -> None:
        """Teardown: make sure the process is gone, whatever happened in the test."""
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.log.close()
