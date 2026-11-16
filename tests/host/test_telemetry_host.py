# SPDX-License-Identifier: MIT
"""Host integration tests for hvacd's MQTT telemetry (Ext 3): the real daemon against a fake sysfs
tree and a private mosquitto broker on 127.0.0.1:18830. The full timing tests (period, broker
outage, reconnection) run on the emulated board (tests/guest/test_mqtt.py).
"""
import signal
import sys
from pathlib import Path
from typing import Iterator

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from hvacd_runner import REG_ALARMS, REG_OUT, REG_STATE, REG_TEMP, Daemon, wait_until  # noqa: E402
from modbus_client import to_signed16  # noqa: E402
from mqtt_helpers import (Broker, Subscriber, mqtt_config_lines, status_topic,  # noqa: E402
                          telemetry_topic)
from test_daemon_host import HVACD, PORT, FakeIio, config_for  # noqa: E402

DEVICE = "hvac-01"
PERIOD_S = 1
STATE_NAMES = ["IDLE", "COOLING", "HEATING", "FAULT"]
REG_COMPRESSOR_STARTS, REG_DIAGNOSTICS = 6, 8


@pytest.fixture
def fake(tmp_path: Path) -> FakeIio:
    f = FakeIio(tmp_path / "iio")
    f.create()
    return f


@pytest.fixture
def broker(tmp_path: Path) -> Iterator[Broker]:
    b = Broker(tmp_path)
    b.start()
    try:
        yield b
    finally:
        b.stop()


@pytest.fixture
def sub(tmp_path: Path, broker: Broker) -> Iterator[Subscriber]:
    s = Subscriber(tmp_path, f"hvac/{DEVICE}/#", "sub")
    try:
        yield s
    finally:
        s.close()


def start_daemon(tmp_path: Path, fake: FakeIio) -> Daemon:
    conf = config_for(tmp_path, fake)
    with open(conf, "a", encoding="utf-8") as f:
        f.write(mqtt_config_lines(PERIOD_S, DEVICE))
    d = Daemon([str(HVACD), "--config", str(conf)], tmp_path / "hvacd.log", PORT)
    d.wait_ready(5.0)
    return d


def test_telemetry_matches_modbus_registers(tmp_path: Path, fake: FakeIio, sub: Subscriber) -> None:
    d = start_daemon(tmp_path, fake)
    try:
        assert wait_until(lambda: len(sub.messages(telemetry_topic(DEVICE))) >= 3, 10.0, 0.1)
        regs = d.regs()
        last = sub.messages(telemetry_topic(DEVICE))[-1].json()
    finally:
        d.kill()
    assert sub.payloads(status_topic(DEVICE))[0] == "online"
    assert list(last) == ["device", "seq", "uptime_s", "temp_c", "state", "compressor", "heater",
                          "fan", "alarms", "fdd", "compressor_starts"]
    assert last["device"] == DEVICE
    assert round(last["temp_c"] * 100) == to_signed16(regs[REG_TEMP]) == 2500
    assert last["state"] == STATE_NAMES[regs[REG_STATE]]
    outputs = int(last["compressor"]) | int(last["heater"]) << 1 | int(last["fan"]) << 2
    assert outputs == regs[REG_OUT]
    assert last["alarms"] == regs[REG_ALARMS]
    assert last["fdd"] == regs[REG_DIAGNOSTICS]
    assert last["compressor_starts"] == regs[REG_COMPRESSOR_STARTS]


def test_daemon_runs_without_broker_and_connects_later(tmp_path: Path, fake: FakeIio) -> None:
    """A missing broker is not a start-up error: control and Modbus work, telemetry starts later."""
    d = start_daemon(tmp_path, fake)
    broker = Broker(tmp_path)
    s = None
    try:
        assert wait_until(lambda: d.regs()[REG_TEMP] == 2500, 2.0)
        broker.start()
        s = Subscriber(tmp_path, f"hvac/{DEVICE}/#", "late")
        # Back-off is 5 s, so the next attempt comes within 5 s; 15 s is the plan's limit.
        assert wait_until(lambda: "online" in s.payloads(status_topic(DEVICE)), 15.0, 0.1)
        assert wait_until(lambda: len(s.messages(telemetry_topic(DEVICE))) >= 1, 5.0, 0.1)
        assert d.proc.poll() is None
    finally:
        d.kill()
        if s is not None:
            s.close()
        broker.stop()
    assert "MQTT connected to 127.0.0.1:18830" in d.log_path.read_text()


def test_kill_9_publishes_offline_last_will(tmp_path: Path, fake: FakeIio, sub: Subscriber) -> None:
    d = start_daemon(tmp_path, fake)
    try:
        assert wait_until(lambda: "online" in sub.payloads(status_topic(DEVICE)), 10.0, 0.1)
        d.proc.send_signal(signal.SIGKILL)  # no chance to say goodbye: the broker's will does
        assert wait_until(lambda: sub.payloads(status_topic(DEVICE))[-1:] == ["offline"], 10.0, 0.1)
    finally:
        d.kill()


def test_clean_shutdown_publishes_offline(tmp_path: Path, fake: FakeIio, sub: Subscriber) -> None:
    d = start_daemon(tmp_path, fake)
    try:
        assert wait_until(lambda: "online" in sub.payloads(status_topic(DEVICE)), 10.0, 0.1)
        assert d.stop(3.0) == 0
        assert wait_until(lambda: sub.payloads(status_topic(DEVICE))[-1:] == ["offline"], 5.0, 0.1)
    finally:
        d.kill()


def test_bad_device_id_exits_one_with_message(tmp_path: Path, fake: FakeIio) -> None:
    conf = config_for(tmp_path, fake)
    with open(conf, "a", encoding="utf-8") as f:
        f.write(mqtt_config_lines(PERIOD_S, "hvac/01"))
    d = Daemon([str(HVACD), "--config", str(conf)], tmp_path / "hvacd.log", PORT)
    assert d.proc.wait(timeout=10) == 1
    d.kill()
    assert "mqtt_device_id may contain only letters, digits and '-'" in d.log_path.read_text()
