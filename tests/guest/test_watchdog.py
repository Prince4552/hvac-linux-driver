# SPDX-License-Identifier: MIT
"""Ext 1: watchdog supervision of hvacd, with the kernel's software watchdog (softdog).

softdog is loaded with soft_noboot=1, so an expiry only logs "softdog: Triggered - Reboot ignored"
(text from drivers/watchdog/softdog.c in the pinned kernel) instead of rebooting the test board.
"""
import os
import signal
import time
from pathlib import Path
from typing import Iterator

import pytest

import hvac_test_lib as h
from hvacd_runner import Daemon, wait_until, write_short_timer_config

HVACD = "/usr/sbin/hvacd"
PORT = 15020
LOOP_MS = 200
TIMEOUT_S = 2
EXPIRY_TEXT = "softdog: Triggered - Reboot ignored"


def disarm_watchdog() -> None:
    """Magic close from the test itself. A daemon that was killed (not stopped cleanly) leaves the
    watchdog running, and the kernel then refuses to unload softdog until it is disarmed."""
    with open("/dev/watchdog", "w", encoding="ascii") as f:
        f.write("V")


@pytest.fixture
def softdog() -> Iterator[None]:
    h.run(["modprobe", "softdog", "soft_noboot=1"])
    try:
        assert h.wait_until(lambda: os.path.exists("/dev/watchdog"), 10.0)
        yield
    finally:
        disarm_watchdog()  # runs after the daemon fixture has ended the process
        h.run(["modprobe", "-r", "softdog"])


@pytest.fixture
def daemon(hvac_device: int, softdog: None, tmp_path: Path) -> Iterator[Daemon]:
    conf = write_short_timer_config(tmp_path / "hvacd.conf", h.IIO_DEVICES, PORT, LOOP_MS)
    with open(conf, "a", encoding="utf-8") as f:
        f.write(f"watchdog_enabled = 1\nwatchdog_device = /dev/watchdog\n"
                f"watchdog_timeout_s = {TIMEOUT_S}\n")
    d = Daemon([HVACD, "--config", str(conf)], tmp_path / "hvacd.log", PORT)
    try:
        d.wait_ready(20.0)
        yield d
    finally:
        if d.proc.poll() is None:
            d.proc.send_signal(signal.SIGCONT)  # a stopped process cannot die until continued
        d.kill()


def expiry_seen(marker: str) -> bool:
    return any(EXPIRY_TEXT in line for line in h.kernel_log_since(marker))


def test_healthy_daemon_keeps_watchdog_quiet(daemon: Daemon) -> None:
    marker = h.kernel_log_marker()
    # Polls for 2.5 x timeout; the expiry message must never appear.
    assert not wait_until(lambda: expiry_seen(marker), 2.5 * TIMEOUT_S, 0.2)
    assert daemon.proc.poll() is None


def test_frozen_daemon_triggers_watchdog(daemon: Daemon) -> None:
    marker = h.kernel_log_marker()
    daemon.proc.send_signal(signal.SIGSTOP)  # simulates a hung control loop
    start = time.monotonic()
    assert wait_until(lambda: expiry_seen(marker), TIMEOUT_S + 3.0, 0.1)
    latency_s = round(time.monotonic() - start, 2)
    h.record_metric("watchdog_trigger_latency_s", latency_s, "s",
                    "Time from the daemon freezing to the watchdog expiring (timeout 2 s)",
                    target=f"<= {TIMEOUT_S + 3}")
    assert latency_s <= TIMEOUT_S + 3.0


def test_clean_shutdown_disarms_watchdog(daemon: Daemon) -> None:
    assert daemon.stop(5.0) == 0
    marker = h.kernel_log_marker()
    assert not wait_until(lambda: expiry_seen(marker), TIMEOUT_S + 3.0, 0.2)
