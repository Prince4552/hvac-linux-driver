# Phase 3: Complete driver (IIO, conversion, alarm limits, KUnit)

## 1. What this phase adds

The driver now reports temperatures. User space reads `in_temp_raw` and `in_temp_scale` under
`/sys/bus/iio/devices/iio:deviceN/`, and it can read and set two alarm limits in millidegrees.
The conversion math is tested inside the kernel with KUnit, for all 4096 possible values.

## 2. Why it matters

A thermostat is only as good as its temperature reading. Getting the sign, the byte order or the
rounding wrong gives readings that look plausible but are wrong. A system like that cools a cold room
or misses a freezing one. Writes from user space must be validated, because the kernel cannot trust
anything a program sends it.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `driver/hvac_temp_conv.h` | Four pure conversion functions (register, raw, millidegrees) |
| `driver/hvac_temp_main.c` | IIO channel, `read_raw()` under the mutex, IIO registration |
| `driver/hvac_temp_alarm.c` | `alarm_low_millicelsius` / `alarm_high_millicelsius` (validated writes) |
| `driver/hvac_temp_kunit.c` | KUnit suite `hvac_temp_conv` (5 cases), its own module |
| `driver/Kbuild` | Builds `hvac_temp.ko` and, with KUnit, `hvac_temp_kunit.ko` |
| `tests/guest/test_driver_conversion.py` | Reference table, byte-order cases, low bits |
| `tests/guest/test_driver_attributes.py` | Name, scale, file modes, read-only channel files |
| `tests/guest/test_driver_alarms.py` | Write/read-back, rounding, invalid input |
| `tests/guest/test_kunit.py` | Loads the KUnit module and checks every case passed |

## 4. How it works

```
 chip register 0x00 (16 bits, sent MSB first)      0x1900  = 0001 1001 0000 0000
   i2c_smbus_read_word_swapped()  -> u16           0x1900
   >> 4  (drop the 4 unused low bits)               0x190   = 400
   sign_extend32(.., 11)  (bit 11 is the sign)      400      (0xE70 would become -400)
 in_temp_raw                                        "400"
 in_temp_scale                                      "62.500000"   (millidegrees per step)
 user space: 400 x 62.5                             25000 m°C = 25.000 °C
```

1. **Registers and I2C/SMBus.** The chip is a few 16-bit "boxes" (registers). An SMBus "read word"
   transaction sends the register number and gets two bytes back.
2. **Byte order.** The chip sends the most significant byte first; SMBus words are least significant
   first. `_swapped()` fixes that. The tests use registers whose two bytes differ (for example 0x1230),
   so a missing swap would be caught. The mutation test in Phase 4 proves this.
3. **Two's complement.** Negative temperatures have the top bit set. `sign_extend32(x, 11)` copies bit 11
   upwards, so 0xFFF becomes -1.
4. **IIO raw and scale.** The driver reports the raw integer and a constant scale. User space multiplies
   them. This keeps floating point out of the kernel (the kernel cannot use the FPU in normal code).
5. **Alarm writes.** `kstrtoint()` parses the text, and a value outside -40000..125000 (or not a number,
   or too big for an int) gives `EINVAL`. The value is rounded to the nearest 1/16 °C step and written
   under the mutex. Reading back converts the stored step to millidegrees, so 62 reads back as 63.
6. **KUnit.** A test framework that runs inside the kernel. Loading `hvac_temp_kunit.ko` runs the
   suite; results appear in debugfs in KTAP (a "TAP" text format: `ok 1 name`).

The driver does not require `alarm_low < alarm_high`, because the chip does not either.

## 5. Run it yourself

```
make run
D=$(grep -l hvac_temp /sys/bus/iio/devices/*/name | xargs dirname)
cat $D/in_temp_raw $D/in_temp_scale          # 400 / 62.500000 (demo sets 25 C)
echo 27000 > $D/alarm_high_millicelsius; cat $D/alarm_high_millicelsius   # 27000
echo abc > $D/alarm_high_millicelsius          # write error: Invalid argument
modprobe hvac_temp_kunit; cat /sys/kernel/debug/kunit/hvac_temp_conv/results
```

## 6. Tests and results

Gate `make test-all` on 2026-11-15: host lint clean; guest **83 passed, 0 failed, 0 skipped**.

| Test file | Cases | What it checks | Result |
| --- | --- | --- | --- |
| `test_driver_conversion.py` | 12 | 7 reference values, 3 byte-order cases, ignored low bits, helper mirror | pass |
| `test_driver_attributes.py` | 5 | name `hvac_temp`, scale `62.500000`, alarm modes, channel files reject writes | pass |
| `test_driver_alarms.py` | 25 | 7 values x 2 files incl. rounding (62 -> 63, 31 -> 0); 5 invalid inputs x 2 files; low > high allowed | pass |
| `test_kunit.py` | 1 | KUnit `hvac_temp_conv`: 5/5 cases (incl. all 4096 raw values) | pass |
| Earlier phases | 40 | platform and probe | pass |

Metric: `kunit_cases_passed` = 5.

What the first run taught (recorded in DECISIONS.md): `i2cset` refuses an address a driver has
claimed, so the test helpers use `-f` (D-017). The IIO core gives channel files mode 0644, so
"read-only" is tested as behaviour (D-016).

**Honest limit:** `i2c-stub` cannot fail a read after probe, so the I2C read-error path in `read_raw()`
is covered by the missing-chip test, the Phase 4 remove-while-reading test and code review (D-018).

## 7. Key concepts

- **sysfs input validation.** Everything written to a sysfs file is untrusted text: parse, range-check,
  reject with `EINVAL`. `sysfs_emit()` writes output without any risk of overflowing the page.
- **Why no floating point in the kernel.** The kernel does not save FPU registers on every entry. Using
  them would corrupt user programs' calculations. Integer millidegrees are exact anyway.
- **Mutex around the I2C conversation.** One lock per device keeps a reader and a writer from mixing
  their transfers. A mutex (not a spinlock) because I2C transfers can sleep.

## 8. What could go wrong

- Shifting a signed value, or forgetting sign extension: -0.0625 °C would read as +255.9 °C. The
  reference table and the round trip over all 4096 values catch this.
- `kstrtoint()` returns `-ERANGE` for huge numbers; the plan wants `EINVAL`, so the driver maps every
  parse error to `-EINVAL` (tested with `99999999999`).
- An empty write (0 bytes) never reaches the driver (sysfs drops it); `echo > file` sends `"\n"`, which is
  what the "empty" test uses.

## 9. Interview questions

1. *How is a negative temperature encoded?* Two's complement in 12 bits: 0xFFF is -1 step (-0.0625 °C);
   the driver sign-extends bit 11.
2. *Why report raw + scale instead of degrees?* No floating point in the kernel, and IIO's convention lets
   any program compute `raw x scale` in its own units.
3. *What does `i2c_smbus_read_word_swapped` do?* Reads a 16-bit register and swaps the bytes because the
   chip sends the high byte first while SMBus expects the low byte first.
4. *What is KUnit?* The kernel's unit-test framework; tests run inside the kernel and report in KTAP.
5. *How do you validate a sysfs write?* `kstrtoint`, range check, return `-EINVAL`, only then touch the hardware.
