# SPDX-License-Identifier: MIT
"""Ext 3: MQTT telemetry from hvacd on the emulated board, with a private broker on port 18830.

Checks the plan's rules (PROJECT_PLAN.md Ext 3, decision D-029): messages arrive every
mqtt_period_s (within 20 %), the payload matches the Modbus registers, kill -9 leads to the
retained "offline" last will, a broker outage does not change the control-loop period by more
than 100 ms while Modbus keeps answering, and a restarted broker sees hvacd back within 15 s.
"""
import signal
import time
from pathlib import Path
from typing import Iterator

import pytest

import hvac_test_lib as h
from hvacd_runner import (REG_ALARMS, REG_OUT, REG_STATE, REG_TEMP, Daemon, wait_until,
                          write_short_timer_config)
from modbus_client import to_signed16
from mqtt_helpers import (Broker, Subscriber, mqtt_config_lines, status_topic,
                          telemetry_topic)

HVACD = "/usr/sbin/hvacd"
PORT = 15020
LOOP_MS = 200
PERIOD_S = 1
DEVICE = "hvac-01"
STATE_NAMES = ["IDLE", "COOLING", "HEATING", "FAULT"]
REG_COMPRESSOR_STARTS, REG_DIAGNOSTICS = 6, 8
RAW_25C = 400  # 400 x 0.0625 = 25.0 C
OUTAGE_S = 12.0  # long enough for two reconnect attempts (5 s back-off)


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


@pytest.fixture
def daemon(hvac_device: int, sub: Subscriber, tmp_path: Path) -> Iterator[Daemon]:
    h.set_temp_raw(RAW_25C)
    conf = write_short_timer_config(tmp_path / "hvacd.conf", h.IIO_DEVICES, PORT, LOOP_MS)
    with open(conf, "a", encoding="utf-8") as f:
        f.write(mqtt_config_lines(PERIOD_S, DEVICE))
    d = Daemon([HVACD, "--config", str(conf)], tmp_path / "hvacd.log", PORT)
    try:
        d.wait_ready(20.0)
        assert wait_until(lambda: "online" in sub.payloads(status_topic(DEVICE)), 15.0, 0.1)
        yield d
    finally:
        d.kill()


def telemetry(sub: Subscriber) -> list:
    return sub.messages(telemetry_topic(DEVICE))


def tick_times_ms(log_path: Path) -> list[int]:
    """Control-tick times (ms since start) from hvacd's debug log: one 'tick <ms>' line per tick."""
    lines = log_path.read_text().splitlines()
    return [int(ln.rsplit(" ", 1)[1]) for ln in lines if " DEBUG tick " in ln]


def test_messages_arrive_at_the_configured_period(daemon: Daemon, sub: Subscriber) -> None:
    assert wait_until(lambda: len(telemetry(sub)) >= 11, 30.0, 0.2)
    msgs = telemetry(sub)[:11]
    gaps = [b.time_s - a.time_s for a, b in zip(msgs, msgs[1:])]
    assert all(0.8 * PERIOD_S <= g <= 1.2 * PERIOD_S for g in gaps), gaps
    seqs = [m.json()["seq"] for m in msgs]
    ratio = round(len(msgs) / (seqs[-1] - seqs[0] + 1), 3)
    h.record_metric("mqtt_messages_received_ratio", ratio, "ratio",
                    "Telemetry messages received / sent over 10 healthy periods", target="== 1.0")
    assert ratio == 1.0, seqs


def test_payload_matches_modbus_registers(daemon: Daemon, sub: Subscriber) -> None:
    assert wait_until(lambda: len(telemetry(sub)) >= 2, 10.0, 0.1)
    regs = daemon.regs()
    last = telemetry(sub)[-1].json()
    assert last["device"] == DEVICE
    assert round(last["temp_c"] * 100) == to_signed16(regs[REG_TEMP]) == 2500
    assert last["state"] == STATE_NAMES[regs[REG_STATE]]
    outputs = int(last["compressor"]) | int(last["heater"]) << 1 | int(last["fan"]) << 2
    assert outputs == regs[REG_OUT]
    assert last["alarms"] == regs[REG_ALARMS]
    assert last["fdd"] == regs[REG_DIAGNOSTICS]
    assert last["compressor_starts"] == regs[REG_COMPRESSOR_STARTS]


def test_kill_9_publishes_offline_last_will(daemon: Daemon, sub: Subscriber) -> None:
    daemon.proc.send_signal(signal.SIGKILL)
    assert wait_until(lambda: sub.payloads(status_topic(DEVICE))[-1:] == ["offline"], 10.0, 0.1)


def test_broker_outage_does_not_disturb_control(daemon: Daemon, broker: Broker) -> None:
    before = len(tick_times_ms(daemon.log_path))
    broker.stop()
    deadline = time.monotonic() + OUTAGE_S
    modbus_reads = 0
    while time.monotonic() < deadline:
        assert daemon.regs()[REG_TEMP] == 2500  # Modbus keeps answering throughout
        modbus_reads += 1
        time.sleep(0.25)
    ticks = tick_times_ms(daemon.log_path)[before:]
    assert len(ticks) >= int(OUTAGE_S * 1000 / LOOP_MS) - 2
    jitter_ms = max(abs((b - a) - LOOP_MS) for a, b in zip(ticks, ticks[1:]))
    h.record_metric("control_loop_jitter_broker_down_ms", jitter_ms, "ms",
                    "Largest change of the 200 ms control-loop period while the broker is down",
                    target="<= 100")
    assert jitter_ms <= 100
    assert modbus_reads >= 40
    assert "MQTT connection to 127.0.0.1:18830 lost" in daemon.log_path.read_text()


def test_restarted_broker_sees_daemon_within_15_s(daemon: Daemon, broker: Broker,
                                                  tmp_path: Path) -> None:
    broker.stop()
    assert wait_until(lambda: "MQTT connection to 127.0.0.1:18830 lost"
                      in daemon.log_path.read_text(), 10.0, 0.1)
    broker.start()
    start = time.monotonic()
    late = Subscriber(tmp_path, status_topic(DEVICE), "after-restart")
    try:
        # No persistence: the restarted broker knows nothing until hvacd publishes "online" again.
        assert wait_until(lambda: "online" in late.payloads(status_topic(DEVICE)), 15.0, 0.1)
        reconnect_s = round(time.monotonic() - start, 2)
    finally:
        late.close()
    h.record_metric("mqtt_reconnect_s", reconnect_s, "s",
                    "Time from broker restart until hvacd is connected again", target="<= 15")
    assert reconnect_s <= 15.0
