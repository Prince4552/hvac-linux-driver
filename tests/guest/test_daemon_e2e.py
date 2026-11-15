# SPDX-License-Identifier: MIT
"""Phase 6 end-to-end tests on the board: driver + daemon + Modbus together.

The real hvac_temp driver reads the simulated chip; hvacd reads the driver through IIO; the tests
talk to hvacd over Modbus TCP (port 15020, short timers) and change the "room temperature" by
writing the chip register. Also records the daemon's latency, memory and size metrics.
"""
import statistics
import subprocess
import time
from pathlib import Path
from typing import Iterator

import pytest

import hvac_test_lib as h
from hvacd_runner import (ALARM_SENSOR_FAULT, ALARM_SETPOINT_REJECTED, REG_ALARMS, REG_COOL,
                          REG_HEAT, REG_OUT, REG_TEMP, STATE_COOLING, STATE_FAULT, STATE_IDLE,
                          Daemon, wait_until, write_short_timer_config)

HVACD = "/usr/sbin/hvacd"
PORT = 15020
LOOP_MS = 200
LOOP_S = LOOP_MS / 1000
FAULT_TARGET_S = 3 * LOOP_S + 1.0     # metric target from PROJECT_PLAN.md section 9
ROUNDTRIP_READS = 100
RSS_AFTER_S = 30.0


@pytest.fixture
def daemon(hvac_device: int, tmp_path: Path) -> Iterator[Daemon]:
    h.set_temp_raw(400)  # 25 C
    conf = write_short_timer_config(tmp_path / "hvacd.conf", h.IIO_DEVICES, PORT, LOOP_MS)
    d = Daemon([HVACD, "--config", str(conf)], tmp_path / "hvacd.log", PORT)
    try:
        d.wait_ready(20.0)
        yield d
    finally:
        d.kill()


def test_temperature_register_matches_chip(daemon: Daemon) -> None:
    assert wait_until(lambda: daemon.regs()[REG_TEMP] == 2500, 5.0)
    h.set_temp_raw(337)  # 21.0625 C
    assert wait_until(lambda: daemon.regs()[REG_TEMP] == 2106, 5.0)


def test_state_follows_chip_temperature(daemon: Daemon) -> None:
    h.set_temp_raw(440)  # 27.5 C
    assert wait_until(daemon.state_is(STATE_COOLING), 5.0)
    assert daemon.regs()[REG_OUT] == 0b101
    h.set_temp_raw(392)  # 24.5 C, cooling stops after min_run (2 s)
    assert wait_until(daemon.state_is(STATE_IDLE), 8.0)


def test_setpoint_writes_are_validated(daemon: Daemon) -> None:
    # Applied: at 25 C a cooling setpoint of 23.5 C must start cooling (a read-back alone would
    # pass before the daemon has even looked at the write).
    assert wait_until(daemon.state_is(STATE_IDLE), 5.0)
    with daemon.client() as client:
        client.write_registers(REG_COOL, [2350, 1900])
    assert wait_until(daemon.state_is(STATE_COOLING), 5.0)
    # Rejected: 41 C is out of range; the previous (applied) value comes back and bit1 is set.
    with daemon.client() as client:
        client.write_register(REG_COOL, 4100)
    assert wait_until(lambda: daemon.regs()[REG_ALARMS] & ALARM_SETPOINT_REJECTED != 0, 5.0)
    assert daemon.regs()[REG_COOL:REG_HEAT + 1] == [2350, 1900]


def test_device_removal_gives_fault_and_recreation_recovers(daemon: Daemon) -> None:
    assert wait_until(daemon.state_is(STATE_IDLE), 5.0)
    h.delete_device(h.CHIP_ADDR)
    start = time.monotonic()
    assert wait_until(daemon.state_is(STATE_FAULT), FAULT_TARGET_S + 5.0)
    latency_s = round(time.monotonic() - start, 2)
    h.record_metric("fault_detection_latency_s", latency_s, "s",
                    "Time from sensor loss (device deleted) to the alarm being visible over Modbus")
    assert daemon.regs()[REG_ALARMS] & ALARM_SENSOR_FAULT
    assert latency_s <= FAULT_TARGET_S
    h.new_device(h.CHIP_ADDR)
    assert wait_until(daemon.state_is(STATE_IDLE), 10.0)
    assert daemon.regs()[REG_ALARMS] & ALARM_SENSOR_FAULT == 0


def test_modbus_round_trip_and_memory_metrics(daemon: Daemon) -> None:
    started = time.monotonic()
    samples_ms = []
    with daemon.client() as client:
        for _ in range(ROUNDTRIP_READS):
            t0 = time.perf_counter()
            client.read_registers(0, 10)
            samples_ms.append((time.perf_counter() - t0) * 1000)
    h.record_metric("modbus_roundtrip_median_ms", round(statistics.median(samples_ms), 2), "ms",
                    "Time for a Modbus request and reply on the emulated board (relative only)")
    assert wait_until(lambda: time.monotonic() - started >= RSS_AFTER_S, RSS_AFTER_S + 5.0, 0.5)
    with open(f"/proc/{daemon.proc.pid}/status", encoding="utf-8") as f:
        rss_kb = next(int(line.split()[1]) for line in f if line.startswith("VmRSS:"))
    h.record_metric("hvacd_rss_kb", rss_kb, "kB", "Daemon memory in use (resident) after 30 s")
    h.record_metric("hvacd_binary_bytes", Path(HVACD).stat().st_size, "bytes",
                    "Size of the stripped hvacd program on the board")
    assert daemon.proc.poll() is None


def test_sigterm_exits_zero(daemon: Daemon) -> None:
    assert daemon.stop(2.0) == 0


def test_bad_config_exits_one_with_clear_message(tmp_path: Path) -> None:
    conf = tmp_path / "bad.conf"
    conf.write_text("loop_period_ms = 50\n")
    proc = subprocess.run([HVACD, "--config", str(conf)], capture_output=True, text=True,
                          timeout=20)
    assert proc.returncode == 1
    assert "config:1: value 50 for 'loop_period_ms' is out of range 100..10000" in proc.stderr


def test_version_flag() -> None:
    out = subprocess.run([HVACD, "--version"], capture_output=True, text=True, timeout=20)
    assert out.returncode == 0 and out.stdout.strip() == "hvacd 1.0.0"
