# Phase 6: Thermostat daemon on the board (sensor, Modbus, end to end)

## 1. What this phase adds

`hvacd`, the thermostat program that runs on the board. It reads the temperature from our driver
through IIO (Industrial I/O) files, runs the control core every loop period, and switches (logs) the
compressor, heater and fan. It also exposes everything as Modbus TCP registers, so a building
management system (BMS) can monitor and adjust it.

## 2. Why it matters

In a building, a thermostat is not alone. A BMS polls hundreds of devices over industrial protocols such
as Modbus or BACnet, to show their state and change setpoints. A safe controller treats every remote
write as untrusted, exactly like the driver treats sysfs writes.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `thermostat/src/main.c` | Arguments, signals, start-up, the `select()` loop, one control tick |
| `thermostat/src/sensor_iio.c` | Finds the IIO device by name, reads raw and scale, converts with integers |
| `thermostat/src/modbus_regs.c` | Pure mapping between daemon status and the 10 Modbus registers |
| `thermostat/src/modbus_server.c` | libmodbus TCP server, up to N clients, no blocking |
| `thermostat/src/log.c` | `<UTC time> hvacd <LEVEL> <message>` lines on stderr |
| `thermostat/tests/test_sensor_iio.c`, `test_modbus_regs.c` | 15 more unit tests (fake sysfs folder) |
| `thermostat/config/hvacd.conf` | Default configuration (installed as `/etc/hvacd.conf`) |
| `br2-external/package/hvacd/` | Buildroot `cmake-package`, depends on libmodbus |
| `.../init.d/S90hvac-demo` | Demo boot: chip at 25 C, driver, device, `hvacd` in the background |
| `tests/common/modbus_client.py` | Standard-library Modbus TCP client + small command-line tool |
| `tests/common/hvacd_runner.py` | Start/stop/query hvacd; shared by host and guest tests |
| `tests/host/test_daemon_host.py` | 9 integration tests with a fake sysfs tree, including a Valgrind run |
| `tests/guest/test_daemon_e2e.py` | 8 end-to-end tests on the board, plus daemon metrics |

## 4. How it works

```
             +------------------- hvacd (one thread) --------------------+
             |  loop:                                                     |
             |    now >= next tick?  -- yes --> control tick:             |
             |                                   1. Modbus setpoint writes? validate / restore
 sysfs  ---> |                                   2. read in_temp_raw x in_temp_scale
             |                                   3. hvac_core_step()                    |
             |                                   4. log state / output changes          |
             |                                   5. publish the 10 registers            |
             |    otherwise: select() on Modbus sockets until the next tick |
 TCP :502 <->|      serve one request per readable client                 |
             +------------------------------------------------------------+
```

1. **One `select()` loop, no threads.** `select()` sleeps until a client sends something *or* the next
   tick is due. With a single thread, nothing can race, and the code reads top to bottom.
2. **Reading the sensor.** Find `iio:deviceN` whose `name` is `hvac_temp`. Read `in_temp_raw` (400) and
   `in_temp_scale` ("62.500000"). Parse the scale as fixed point (62500000 micro-units) and compute
   `400 x 62500000 / 1000000 = 25000` m°C, rounded half away from zero in 64-bit integers. On any
   failure, the core gets `read_ok = false` and the device is searched for again next tick.
3. **Modbus TCP.** The client sends "read 10 holding registers from address 0" (function 03) and the
   server answers with ten 16-bit words. Writes use functions 06 (one register) or 16 (several).
   Temperatures are centidegrees in two's complement (2450 = 24.50 °C, 0xFDF3 = -5.25 °C).
4. **Setpoint validation.** libmodbus accepts a write straight into the table. On the next tick, hvacd
   compares the setpoint registers with what it last published. If they differ, it validates both with
   the core's rules. Valid values are applied. Invalid values put the old ones back and set alarm bit1.
5. **Signals.** SIGTERM/SIGINT only set a flag. The loop sees it, closes the server and frees libmodbus,
   then exits with code 0.

## 5. Run it yourself

```
make run                                                     # boots in demo mode, hvacd starts
# in another terminal on the host:
python3 tests/common/modbus_client.py --port 5020 read 0 10  # 0: 2500 (25.00 C), 1: 0 (IDLE), ...
python3 tests/common/modbus_client.py --port 5020 write 2 2350   # cooling setpoint 23.50 C
python3 tests/common/modbus_client.py --port 5020 read 1 1   # 1: 1 (COOLING)
# in the board console:
tail /var/log/hvacd.log    # ... hvacd INFO state IDLE -> COOLING temp=25.00C
```

## 6. Tests and results

Gate `make test-all` on 2026-11-15: host **76 passed** (67 unit tests x3 + 9 integration), guest **98
passed**, 0 failed, 0 skipped.

| Test | What it checks | Result |
| --- | --- | --- |
| `test_sensor_iio.c` (9) | Scale parsing (`62.500000`, `1.000000`, `0.5`, malformed), negative rounding, discovery, missing files, rediscovery | pass |
| `test_modbus_regs.c` (6) | Centidegree rounding, two's complement, bits, saturation, setpoint decoding | pass |
| Host integration (9) | Temperature, state, valid/invalid setpoint writes, FAULT within 3 loop periods, recovery, SIGTERM -> 0, bad config -> 1, Valgrind 0 errors | pass |
| Guest end to end (8) | Same, with the real driver and chip; bad config; `--version`; metrics | pass |

| Metric | Value | Target | Meaning |
| --- | --- | --- | --- |
| `fault_detection_latency_s` | 0.07 | <= 1.6 (3 x 0.2 s + 1 s) | Device deleted -> FAULT visible over Modbus |
| `modbus_roundtrip_median_ms` | 2.15 | report | One Modbus read on the emulated board |
| `hvacd_rss_kb` | 1412 | report | Daemon memory in use after 30 s |
| `hvacd_binary_bytes` | 26480 | report | Stripped program size |

The first guest run found a weak test: reading a register back right after writing it proves nothing,
because libmodbus stores the write at once. The tests now check the *effect* of a valid write (cooling
starts), and D-025 records this.

## 7. Key concepts

- **User space reading sysfs.** Ordinary file reads. The kernel calls the driver's `read_raw()` behind
  the scenes.
- **`select()` loop.** One thread waits on many sockets and a timeout at once. This is the classic
  structure of small embedded daemons.
- **Modbus holding registers and function codes.** A register is a 16-bit word at an address. Function
  03 reads, 06 writes one, 16 writes several. Modbus TCP wraps each request in a 7-byte header.
- **Fake-filesystem testing.** A temporary folder with the same file names as sysfs lets the real binary
  run on any PC, with no kernel module.
- **End-to-end testing.** Driver, daemon and protocol together on the board, the way a customer uses them.

## 8. What could go wrong

- A client that sends half a request could block a naive server forever. Here a 200 ms timeout bounds it.
- Writing `in_temp_raw` while hvacd reads it could expose half a file. The fake sysfs writes are atomic
  (write a temp file, then rename).
- A port below 1024 needs root on the host, so the host tests use 15020.
- Trusting a read-back after a write (D-025).

## 9. Interview questions

1. *Why a single-threaded select() loop?* No data races and no locks, deterministic timing, and easy to
   review. The work per tick is tiny, so one thread is plenty.
2. *How does a BMS change the setpoint, and what stops a bad value?* It writes holding registers 2/3 over
   Modbus. hvacd validates on the next tick, restores the old values and raises alarm bit1 if they are
   invalid.
3. *What happens when the sensor disappears?* The read fails, the core goes to FAULT (equipment off,
   alarm on) within one tick, and Modbus shows state 3 and alarm bit0. Measured: 0.07 s.
4. *How did you test the daemon for memory errors?* The full scenario under Valgrind memcheck: 0 errors,
   0 definite or indirect leaks. All buffers are fixed-size, and allocation happens only at start-up.
5. *What is two's complement in a Modbus register?* Negative numbers stored as 65536 + value, so -525
   becomes 0xFDF3. The client converts it back.
