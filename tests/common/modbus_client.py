# SPDX-License-Identifier: MIT
"""Minimal Modbus TCP client using only the Python standard library.

Supports the three function codes hvacd serves (PROJECT_PLAN.md section 4.6): 03 read holding
registers, 06 write single register, 16 write multiple registers. Used by host and guest tests,
and handy by hand:

    python3 tests/common/modbus_client.py --host 127.0.0.1 --port 5020 read 0 10
    python3 tests/common/modbus_client.py --port 5020 write 2 2450

A Modbus TCP frame is a 7-byte MBAP header (transaction id, protocol 0, length, unit id) followed
by the function code and its data, all big-endian.
"""
import argparse
import socket
import struct
import sys

FC_READ_HOLDING = 0x03
FC_WRITE_SINGLE = 0x06
FC_WRITE_MULTIPLE = 0x10
EXCEPTION_FLAG = 0x80


class ModbusError(RuntimeError):
    """The server answered with a Modbus exception, or the reply was malformed."""


class ModbusClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 502, unit: int = 1,
                 timeout_s: float = 5.0) -> None:
        self.unit = unit
        self.transaction = 0
        self.sock = socket.create_connection((host, port), timeout=timeout_s)

    def close(self) -> None:
        self.sock.close()

    def __enter__(self) -> "ModbusClient":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def _recv_exact(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ModbusError("connection closed by server")
            data += chunk
        return data

    def _request(self, pdu: bytes) -> bytes:
        """Sends one request PDU and returns the response PDU (function code + data)."""
        self.transaction = (self.transaction + 1) % 0x10000
        header = struct.pack(">HHHB", self.transaction, 0, len(pdu) + 1, self.unit)
        self.sock.sendall(header + pdu)
        tid, proto, length, _unit = struct.unpack(">HHHB", self._recv_exact(7))
        if tid != self.transaction or proto != 0 or length < 2:
            raise ModbusError(f"bad reply header tid={tid} proto={proto} length={length}")
        reply = self._recv_exact(length - 1)
        if reply[0] == pdu[0] | EXCEPTION_FLAG:
            raise ModbusError(f"exception code {reply[1]} for function {pdu[0]}")
        if reply[0] != pdu[0]:
            raise ModbusError(f"unexpected function code {reply[0]}")
        return reply

    def read_registers(self, address: int, count: int) -> list[int]:
        """Function 03: returns count unsigned 16-bit register values."""
        reply = self._request(struct.pack(">BHH", FC_READ_HOLDING, address, count))
        if reply[1] != 2 * count:
            raise ModbusError(f"expected {2 * count} data bytes, got {reply[1]}")
        return list(struct.unpack(f">{count}H", reply[2:2 + 2 * count]))

    def write_register(self, address: int, value: int) -> None:
        """Function 06: writes one register (value 0..65535)."""
        self._request(struct.pack(">BHH", FC_WRITE_SINGLE, address, value & 0xFFFF))

    def write_registers(self, address: int, values: list[int]) -> None:
        """Function 16: writes consecutive registers."""
        data = struct.pack(f">{len(values)}H", *[v & 0xFFFF for v in values])
        self._request(struct.pack(">BHHB", FC_WRITE_MULTIPLE, address, len(values),
                                  len(data)) + data)


def to_signed16(value: int) -> int:
    """Register word -> signed value (two's complement)."""
    return value - 0x10000 if value >= 0x8000 else value


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=502)
    sub = parser.add_subparsers(dest="cmd", required=True)
    read = sub.add_parser("read", help="read ADDRESS COUNT holding registers")
    read.add_argument("address", type=int)
    read.add_argument("count", type=int)
    write = sub.add_parser("write", help="write VALUE... starting at ADDRESS")
    write.add_argument("address", type=int)
    write.add_argument("values", type=int, nargs="+")
    args = parser.parse_args(argv)

    with ModbusClient(args.host, args.port) as client:
        if args.cmd == "read":
            for i, value in enumerate(client.read_registers(args.address, args.count)):
                print(f"{args.address + i}: {value} (signed {to_signed16(value)})")
        elif len(args.values) == 1:
            client.write_register(args.address, args.values[0])
        else:
            client.write_registers(args.address, args.values)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
