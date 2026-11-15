# Phase 2: Driver skeleton and hello module

## 1. What this phase adds

A minimal "hello" kernel module to learn how modules load and unload, and the first version of the
`hvac_temp` driver: it binds to the simulated chip, checks that the chip answers, and logs
`ready at address 0x48`. It does not report temperatures yet (Phase 3).

## 2. Why it matters

Every Linux device driver starts the same way: register with a bus, get "probed" when a matching
device appears, check the hardware, and fail cleanly if it is missing. In an HVAC controller, a sensor
that is unplugged or broken must produce a clear error, never a crash or a fake reading.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `examples/hello_module/hello.c`, `Kbuild` | Minimal module: prints on load and unload |
| `driver/hvac_temp_regs.h` | Register addresses of the chip |
| `driver/hvac_temp.h` | Per-device private data (`client`, `lock`) |
| `driver/hvac_temp_main.c` | I2C driver: ID tables, probe, registration |
| `driver/Kbuild`, `driver/Makefile` | Kernel build rules; out-of-tree build wrapper |
| `br2-external/package/hvac-temp/`, `hvac-hello/` | Buildroot packages (`kernel-module` infrastructure) |
| `tests/guest/test_driver_probe.py` | Probe success, probe failure, unbind, 50 cycles |
| `tests/guest/conftest.py` | New fixture `hvac_device` |

## 4. How it works

```
 user space:  echo "hvac_temp 0x48" > /sys/bus/i2c/devices/i2c-1/new_device
                                   |
 I2C core:    creates client 1-0048, looks for a driver whose id_table has "hvac_temp"
                                   |
 hvac_temp:   probe(client)
                1. adapter can do SMBus word transfers?   no -> -EOPNOTSUPP
                2. devm_iio_device_alloc()  (private data inside)
                3. devm_mutex_init()
                4. read CONFIG register (0x01)            fails -> "chip not responding"
                5. dev_info("ready at address 0x48")
```

1. **Kernel vs user space.** User programs cannot touch hardware directly. They ask the kernel through
   system calls and files under `/sys` and `/dev`. Driver code runs inside the kernel, so a bug there can
   crash the whole machine. That is why drivers are kept small and tested hard.
2. **Bus / device / driver model.** The I2C *bus* knows which *devices* exist (here created by hand with
   `new_device`; on a real board, from the device tree). Each *driver* lists the device names it
   supports. When names match, the kernel calls the driver's `probe()`.
3. **`devm_` (device-managed) resources.** Anything allocated with `devm_*` is freed automatically when
   the device goes away, in reverse order. So the driver needs no `remove()` function, and a whole
   class of leak and ordering bugs cannot happen.
4. **Device tree.** On a real board, this node would create the device at boot instead of `new_device`:

```
&i2c1 {
	temperature-sensor@48 {
		compatible = "hvac,hvac-temp";
		reg = <0x48>;
	};
};
```

## 5. Run it yourself

```
make run                                    # log in as root
modprobe hello; dmesg | tail -1             # hello: loaded
modprobe -r hello; dmesg | tail -1          # hello: unloaded
modprobe i2c-stub chip_addr=0x48            # (the demo init script already does this)
modprobe hvac_temp
B=$(grep -l "SMBus stub" /sys/bus/i2c/devices/i2c-*/name | cut -d/ -f6)
echo "hvac_temp 0x48" > /sys/bus/i2c/devices/$B/new_device
dmesg | tail -1                             # hvac_temp 1-0048: ready at address 0x48
echo "hvac_temp 0x49" > /sys/bus/i2c/devices/$B/new_device
dmesg | tail -2                             # ... chip not responding
```

## 6. Tests and results

Gate `make test-all` on 2026-11-15: host lint clean; guest **40 passed, 0 failed, 0 skipped**
(34 platform + 6 new).

| Test | What it checks | Result |
| --- | --- | --- |
| `test_hello_module_logs_load_and_unload` | `hello: loaded` / `hello: unloaded` in the kernel log | pass |
| `test_driver_module_loads_and_is_listed` | `hvac_temp` appears in `/proc/modules` and unloads | pass |
| `test_probe_succeeds_when_chip_answers` | `ready at address 0x48`, `driver` link points to `hvac_temp` | pass |
| `test_probe_fails_cleanly_when_no_chip` | 0x49: `chip not responding`, no driver link, no IIO device, clean log | pass |
| `test_delete_device_unbinds_cleanly` | Device folder disappears, nothing left behind | pass |
| `test_repeated_load_bind_unbind_unload_cycles_are_clean` | 50 full cycles, clean kernel log (13 s) | pass |

Seen in the kernel log: `hvac_temp 0-0049: error -ENODEV: chip not responding`. `i2c-stub` answers
only at addresses it simulates, so a missing chip looks exactly like a real absent device.
The line `hello: loading out-of-tree module taints kernel.` is normal for any module built outside the
kernel tree; it is not one of the "bad" patterns.

## 7. Key concepts

- **Kernel module.** A piece of kernel code loaded at run time (`modprobe`) instead of compiled in.
  It has an init function (load) and an exit function (unload).
- **Probe.** The driver's "is this my device, and does it work?" function. Returning an error means
  "not bound": the device stays visible but has no driver.
- **`dev_err_probe()`.** Logs a probe error with the device name and error code, in one standard format.
- **`dmesg`.** Prints the kernel's log. The tests read it to check messages, and to check that nothing
  bad (BUG, WARNING, Oops) happened.

## 8. What could go wrong

- Forgetting to unbind before unloading: the kernel handles it, but the tests check it explicitly.
- Probing a missing chip: a wrong driver would bind anyway and report garbage. Ours reads a register
  first, and the 0x49 test proves it refuses.
- Leaks on the error path: impossible here because everything is `devm_`. Phase 4 measures it.

## 9. Interview questions

1. *What happens between `new_device` and `probe()`?* The I2C core creates a client device and matches
   its name against each driver's ID table (or the compatible string against the OF table). On a match,
   it calls `probe()`.
2. *Why no `remove()`?* All resources are device-managed (`devm_`), so the driver core frees them in
   reverse order when the device is removed.
3. *How does the driver know the chip is really there?* It reads the CONFIG register during probe. A
   missing chip makes the I2C transfer fail, and probe returns that error.
4. *What is the device tree?* A data file describing the board's hardware, so the kernel can create
   devices (and pick drivers) without hard-coded board code.
