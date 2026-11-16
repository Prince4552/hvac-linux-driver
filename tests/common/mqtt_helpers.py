# SPDX-License-Identifier: MIT
"""MQTT test helpers (Ext 3), shared by the host and guest test suites.

Broker: a private mosquitto broker on 127.0.0.1:18830 (never the system broker), no persistence.
Subscriber: a mosquitto_sub process whose output goes to a file, one line per message:
"<arrival unix time> <topic> <payload>". Standard library only (it also runs inside the QEMU guest).
"""
import json
import socket
import subprocess
from pathlib import Path
from typing import Optional

from hvacd_runner import wait_until

BROKER_PORT = 18830
BROKER_CONFIG = """\
listener {port} 127.0.0.1
allow_anonymous true
persistence false
"""


def port_open(port: int) -> bool:
    """True if something accepts TCP connections on 127.0.0.1:port."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


class Broker:
    """One mosquitto broker process; can be stopped and started again on the same port."""

    def __init__(self, workdir: Path, port: int = BROKER_PORT) -> None:
        self.port = port
        self.conf = workdir / "mosquitto.conf"
        self.conf.write_text(BROKER_CONFIG.format(port=port))
        self.log_path = workdir / "mosquitto.log"
        self.proc: Optional[subprocess.Popen] = None

    def start(self, timeout_s: float = 10.0) -> None:
        log = open(self.log_path, "a", encoding="utf-8")
        self.proc = subprocess.Popen(["mosquitto", "-c", str(self.conf)], stdout=log, stderr=log)
        log.close()  # the child keeps its own copy of the file
        assert wait_until(lambda: port_open(self.port), timeout_s, 0.05), \
            f"mosquitto did not start: {self.log_path.read_text()}"

    def stop(self, timeout_s: float = 10.0) -> None:
        """SIGTERM and wait; safe to call when already stopped."""
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=timeout_s)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.proc = None


class Message:
    def __init__(self, line: str) -> None:
        when, topic, payload = line.split(" ", 2)
        self.time_s = float(when)
        self.topic = topic
        self.payload = payload

    def json(self) -> dict:
        return json.loads(self.payload)


class Subscriber:
    """mosquitto_sub on a topic filter; messages are read back from its output file."""

    def __init__(self, workdir: Path, topic: str, name: str, port: int = BROKER_PORT) -> None:
        self.out_path = workdir / f"{name}.txt"
        self.out = open(self.out_path, "w", encoding="utf-8")
        # -F: %U = arrival time (seconds.nanoseconds), %t = topic, %p = payload.
        # -q 1: the subscription itself is QoS 1, like the telemetry.
        self.proc = subprocess.Popen(
            ["mosquitto_sub", "-h", "127.0.0.1", "-p", str(port), "-q", "1", "-t", topic,
             "-F", "%U %t %p"], stdout=self.out, stderr=subprocess.STDOUT)

    def messages(self, topic: Optional[str] = None) -> list[Message]:
        lines = [ln for ln in self.out_path.read_text().splitlines() if ln.count(" ") >= 2]
        msgs = [Message(ln) for ln in lines if ln[0].isdigit()]
        return [m for m in msgs if topic is None or m.topic == topic]

    def payloads(self, topic: str) -> list[str]:
        return [m.payload for m in self.messages(topic)]

    def close(self) -> None:
        """SIGKILL, not SIGTERM: mosquitto_sub's SIGTERM handler calls mosquitto_disconnect_v5(),
        which takes library locks and can deadlock (it hung a CI run; D-029 addendum). Every
        message is already in the output file (mosquitto_sub flushes after each one)."""
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait(timeout=10)
        self.out.close()


def telemetry_topic(device: str) -> str:
    return f"hvac/{device}/telemetry"


def status_topic(device: str) -> str:
    return f"hvac/{device}/status"


def mqtt_config_lines(period_s: int, device: str = "hvac-01", port: int = BROKER_PORT) -> str:
    """Lines to append to an hvacd configuration file to enable telemetry."""
    return (f"mqtt_enabled = 1\nmqtt_host = 127.0.0.1\nmqtt_port = {port}\n"
            f"mqtt_device_id = {device}\nmqtt_period_s = {period_s}\n")
