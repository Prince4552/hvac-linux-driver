# SPDX-License-Identifier: MIT
"""Host integration tests for hvacd (Phase 6): the real daemon binary against a fake sysfs tree.

No QEMU needed: a temporary folder imitates /sys/bus/iio/devices/iio:deviceN, and the tests talk
to hvacd over Modbus TCP on port 15020 with short timers. A second test runs the same scenario
under Valgrind and requires zero memory errors and zero definite or indirect leaks.
"""
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterator

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from hvacd_runner import (ALARM_SENSOR_FAULT, ALARM_SETPOINT_REJECTED, REG_ALARMS,  # noqa: E402
                          REG_COOL, REG_HEAT, REG_OUT, REG_STATE, REG_TEMP, STATE_COOLING,
                          STATE_FAULT, STATE_IDLE, Daemon, wait_until, write_short_timer_config)
from modbus_client import to_signed16  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
REG_DIAGNOSTICS = 8
FDD_SENSOR_STUCK = 0b10
HVACD = Path(os.environ.get("HVACD_BIN", ROOT / "build" / "thermostat" / "hvacd"))
PORT = 15020
LOOP_MS = 200
LOOP_S = LOOP_MS / 1000


class FakeIio:
    """A fake IIO device folder: base/iio:device0/{name,in_temp_raw,in_temp_scale}."""

    def __init__(self, base: Path) -> None:
        self.base = base
        self.dev = base / "iio:device0"
        self.raw = 400

    def create(self) -> None:
        self.dev.mkdir(parents=True, exist_ok=True)
        (self.dev / "name").write_text("hvac_temp\n")
        (self.dev / "in_temp_scale").write_text("62.500000\n")
        self.set_raw(self.raw)

    def set_raw(self, raw: int) -> None:
        self.raw = raw
        tmp = self.dev / "in_temp_raw.tmp"
        tmp.write_text(f"{raw}\n")
        tmp.replace(self.dev / "in_temp_raw")  # atomic: the daemon never sees a half file

    def remove(self) -> None:
        shutil.rmtree(self.dev)


@pytest.fixture
def fake(tmp_path: Path) -> FakeIio:
    f = FakeIio(tmp_path / "iio")
    f.create()
    return f


def config_for(tmp_path: Path, fake: FakeIio) -> Path:
    return write_short_timer_config(tmp_path / "hvacd.conf", str(fake.base), PORT, LOOP_MS)


@pytest.fixture
def daemon(tmp_path: Path, fake: FakeIio) -> Iterator[Daemon]:
    assert HVACD.is_file(), f"build hvacd first ({HVACD})"
    d = Daemon([str(HVACD), "--config", str(config_for(tmp_path, fake))],
               tmp_path / "hvacd.log", PORT)
    try:
        d.wait_ready(5.0)
        yield d
    finally:
        d.kill()


def test_temperature_register_matches_fake_sensor(daemon: Daemon, fake: FakeIio) -> None:
    assert wait_until(lambda: daemon.regs()[REG_TEMP] == 2500, 2.0)  # 400 x 62.5 = 25.00 C
    fake.set_raw(337)  # 21.0625 C -> 2106 centidegrees (a believable change, < max_step)
    assert wait_until(lambda: to_signed16(daemon.regs()[REG_TEMP]) == 2106, 2.0)


def test_state_follows_temperature(daemon: Daemon, fake: FakeIio) -> None:
    fake.set_raw(440)  # 27.5 C: above 27 C, cooling starts
    assert wait_until(daemon.state_is(STATE_COOLING), 2.0)
    assert daemon.regs()[REG_OUT] == 0b101  # compressor + fan
    fake.set_raw(392)  # 24.5 C: below 25 C; stops once min_run (2 s) has passed
    assert wait_until(daemon.state_is(STATE_IDLE), 4.0)


def test_valid_setpoint_write_is_applied(daemon: Daemon) -> None:
    """Reading the register back proves nothing (libmodbus stores writes at once), so check the
    effect: at 25 C, a cooling setpoint of 23.5 C (start at 24.5 C) must start cooling."""
    assert wait_until(daemon.state_is(STATE_IDLE), 2.0)
    with daemon.client() as client:
        client.write_registers(REG_COOL, [2350, 1900])
    assert wait_until(daemon.state_is(STATE_COOLING), 2.0)
    assert daemon.regs()[REG_COOL:REG_HEAT + 1] == [2350, 1900]
    assert daemon.regs()[REG_ALARMS] & ALARM_SETPOINT_REJECTED == 0


def test_invalid_setpoint_write_is_reverted_and_flagged(daemon: Daemon) -> None:
    with daemon.client() as client:
        client.write_register(REG_COOL, 4100)  # 41 C: above the 40 C limit
    assert wait_until(lambda: daemon.regs()[REG_ALARMS] & ALARM_SETPOINT_REJECTED != 0, 2.0)
    assert daemon.regs()[REG_COOL] == 2600
    with daemon.client() as client:
        client.write_register(REG_COOL, 2500)  # the next valid write clears the flag
    assert wait_until(lambda: daemon.regs()[REG_ALARMS] & ALARM_SETPOINT_REJECTED == 0, 2.0)
    assert daemon.regs()[REG_COOL] == 2500


def test_sensor_loss_gives_fault_and_recovery_gives_idle(daemon: Daemon, fake: FakeIio) -> None:
    fake.remove()
    start = time.monotonic()
    assert wait_until(daemon.state_is(STATE_FAULT), 3 * LOOP_S)
    assert time.monotonic() - start <= 3 * LOOP_S
    assert daemon.regs()[REG_ALARMS] & ALARM_SENSOR_FAULT
    fake.create()
    assert wait_until(daemon.state_is(STATE_IDLE), 4 * LOOP_S + 0.5)  # 3 good readings
    assert daemon.regs()[REG_ALARMS] & ALARM_SENSOR_FAULT == 0
    log = daemon.log_path.read_text()
    assert "state IDLE -> FAULT" in log and "state FAULT -> IDLE" in log


def test_sigterm_exits_zero_within_two_seconds(daemon: Daemon) -> None:
    assert daemon.stop(2.0) == 0


def test_bad_config_exits_one_with_message(tmp_path: Path) -> None:
    conf = tmp_path / "bad.conf"
    conf.write_text("modbus_port = 15020\ncool_setpont_mc = 25000\n")
    proc = subprocess.run([str(HVACD), "--config", str(conf)], capture_output=True, text=True,
                          timeout=10)
    assert proc.returncode == 1
    assert "config:2: unknown key 'cool_setpont_mc'" in proc.stderr


def test_bad_watchdog_device_exits_one_with_message(tmp_path: Path, fake: FakeIio) -> None:
    """Ext 1: a watchdog that cannot be opened is a start-up error, not something to ignore."""
    conf = config_for(tmp_path, fake)
    with open(conf, "a", encoding="utf-8") as f:
        f.write("watchdog_enabled = 1\nwatchdog_device = /nonexistent/watchdog\n")
    proc = subprocess.run([str(HVACD), "--config", str(conf)], capture_output=True, text=True,
                          timeout=10)
    assert proc.returncode == 1
    assert "cannot open watchdog /nonexistent/watchdog: No such file or directory" in proc.stderr


def test_stuck_sensor_sets_diagnostic_register(tmp_path: Path, fake: FakeIio) -> None:
    """Ext 2: an unchanged reading for fdd_stuck_window_s sets bit1 of register 8, logged once."""
    conf = config_for(tmp_path, fake)
    with open(conf, "a", encoding="utf-8") as f:
        f.write("fdd_stuck_window_s = 60\n")  # the shortest allowed window
    d = Daemon([str(HVACD), "--config", str(conf)], tmp_path / "hvacd.log", PORT)
    try:
        d.wait_ready(5.0)
        assert d.regs()[REG_DIAGNOSTICS] == 0
        assert wait_until(lambda: d.regs()[REG_DIAGNOSTICS] & FDD_SENSOR_STUCK != 0, 70.0, 0.5)
        assert d.stop(2.0) == 0
    finally:
        d.kill()
    assert d.log_path.read_text().count("diagnostic: sensor stuck") == 1


def test_version_and_help() -> None:
    out = subprocess.run([str(HVACD), "--version"], capture_output=True, text=True, timeout=10)
    assert out.returncode == 0 and out.stdout.startswith("hvacd ")
    out = subprocess.run([str(HVACD), "--help"], capture_output=True, text=True, timeout=10)
    assert out.returncode == 0 and "usage" in out.stdout
    out = subprocess.run([str(HVACD)], capture_output=True, text=True, timeout=10)
    assert out.returncode == 1


def test_daemon_is_clean_under_valgrind(tmp_path: Path, fake: FakeIio) -> None:
    valgrind = shutil.which("valgrind")
    assert valgrind is not None, "valgrind is required (apt package valgrind)"
    vg_log = tmp_path / "valgrind.log"
    cmd = [valgrind, "--leak-check=full", "--errors-for-leak-kinds=definite,indirect",
           "--error-exitcode=99", f"--log-file={vg_log}",
           str(HVACD), "--config", str(config_for(tmp_path, fake))]
    d = Daemon(cmd, tmp_path / "hvacd.log", PORT)
    try:
        d.wait_ready(30.0)
        with d.client() as client:
            client.write_register(REG_COOL, 4100)  # rejected write path
            client.write_register(REG_COOL, 2500)  # accepted write path
        fake.set_raw(440)
        assert wait_until(d.state_is(STATE_COOLING), 10.0)
        fake.remove()
        assert wait_until(d.state_is(STATE_FAULT), 10.0)
        fake.create()
        assert wait_until(lambda: d.regs()[REG_STATE] != STATE_FAULT, 10.0)
        code = d.stop(30.0)
    finally:
        d.kill()
    report = vg_log.read_text()
    assert code == 0, report
    assert "ERROR SUMMARY: 0 errors" in report, report
