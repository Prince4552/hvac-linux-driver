# Project plan: HVAC temperature-sensor driver, test suite and thermostat

This is the complete specification. It says what to build, how the parts connect, the exact interfaces,
the phases, the tests and gates for each phase, the metrics to report, and the documents to write.
If something is not specified here, choose the simplest option that fits and record it in `docs/DECISIONS.md`.

Contents:

1. Goal and scope
2. System architecture
3. Repository layout
4. Interfaces and contracts
5. Build platform (Buildroot and QEMU)
6. Test strategy
7. Compulsory phases (Phase 0 to Phase 7)
8. Extension phases (Ext 1 to Ext 4)
9. Metrics catalogue
10. Documentation deliverables
11. Publishing to GitHub and verifying the push
12. Known pitfalls

---

## 1. Goal and scope

**Goal.** A small, complete, well-tested embedded Linux system that mirrors real work at Carrier's
Hyderabad engineering centre: a Linux device driver, automated driver tests, HVAC control software with
equipment-protection rules, an industrial protocol interface, and continuous integration.

**In scope (compulsory):**

- Kernel driver `hvac_temp` for a TMP102-compatible I2C temperature sensor, exposed through IIO.
- A hardware-free driver test suite inside an emulated ARM64 board, including memory-safety checks.
- A thermostat daemon `hvacd` with a pure control core, Modbus TCP interface, and a host simulation
  that measures control quality.
- Build system (Buildroot), emulator (QEMU), one-command test runs, CI on GitHub Actions.
- Plain-language documentation: README, per-phase learning notes, a technical report.

**In scope (extensions, after the core is published):** watchdog supervision, fault detection and
diagnostics, MQTT telemetry, the same control core running on FreeRTOS.

**Out of scope:** real hardware, Yocto, BACnet, graphical user interfaces, cloud deployment, security
hardening beyond the memory-safety rules. Mention these as possible next steps in the report only.

**Success criteria:** every phase gate passes with 0 failures and 0 skips; all metrics in section 9 meet
their targets; a reader with no embedded background can follow the README and the technical report;
anyone can reproduce the results with the commands in the README.

---

## 2. System architecture

### 2.1 Picture

```
 HOST: WSL2 Ubuntu, x86-64 (and GitHub Actions runs the same scripts)
 +----------------------------------------------------------------------------+
 | make test-host : unit tests, sanitizers, coverage, simulation, static checks |
 | make build     : Buildroot -> ARM64 Linux image (kernel + root filesystem)   |
 | make test-qemu : boot the image in QEMU, run in-guest tests, collect results |
 +--------------------------------------+-------------------------------------+
                                        | boots
 +--------------------------------------v-------------------------------------+
 | QEMU ARM64 virtual board                                                     |
 |                                                                            |
 |  USER SPACE                                                                |
 |   hvacd (thermostat daemon)                     pytest guest tests          |
 |    main loop -> sensor_iio -> hvac_core -> modbus_server (TCP 502)          |
 |        | reads in_temp_raw / in_temp_scale                                  |
 |  ------+---------------------------- system calls ------------------------ |
 |  KERNEL|                                                                   |
 |   IIO core: /sys/bus/iio/devices/iio:deviceN/{name,in_temp_raw,...}         |
 |   hvac_temp.ko  (our driver: conversion, alarm limits, mutex)               |
 |   I2C core (SMBus word transfers to address 0x48)                           |
 |   i2c-stub.ko  (simulated chip: 256 registers held in memory)               |
 +----------------------------------------------------------------------------+
```

### 2.2 How one reading flows

1. `hvacd` reads `in_temp_raw` and `in_temp_scale` (system call into the kernel).
2. IIO calls `hvac_temp_read_raw()` in our driver.
3. The driver takes its mutex, reads register 0x00 with `i2c_smbus_read_word_swapped()`, releases the mutex.
4. `i2c-stub` answers from memory (on a real board, the chip answers over two wires).
5. The driver converts the 16-bit register to a signed 12-bit raw value; IIO formats it as text.
6. `hvacd` computes millidegrees = raw x scale, runs `hvac_core_step()`, applies outputs, updates
   its Modbus registers. A building management system (BMS) polls those registers over TCP.

### 2.3 Key design decisions (and why)

| Decision | Reason |
| --- | --- |
| Emulate an ARM64 board with QEMU | No hardware needed; the WSL2 kernel cannot load our modules; ARM is what Carrier ships |
| Build the image with Buildroot | Same job as Yocto (used at Carrier) but learnable in days |
| Simulate the chip with `i2c-stub` | Ships with Linux; lets tests set any register value, including impossible-in-lab ones |
| Expose the sensor through IIO | Linux's standard sensor framework; userspace reads plain files |
| `devm_` resource management, no `remove()` | Removes a whole class of leak and ordering bugs |
| Pure control core (no I/O, no clock, no allocation) | Unit-testable in milliseconds, portable to FreeRTOS unchanged |
| Single-threaded daemon (`select()` loop) | No data races, easy to read |
| Integer arithmetic everywhere in product code | Kernel forbids floating point; integers give identical results on every machine |
| Tests run inside the guest, results printed between markers on the serial console | One simple, reliable channel from guest to host; works the same in CI |

---

## 3. Repository layout

Create exactly this layout (add files inside it as phases require).

```
.
|-- CLAUDE.md                     # instructions for Claude Code (given)
|-- README.md                     # front page: what, why, quick start, results
|-- LICENSE                       # MIT, with note that driver/ is GPL-2.0-only
|-- THIRD_PARTY.md                # every external component, version, license
|-- VERSIONS.md                   # pinned versions and checksums
|-- Makefile                      # top-level commands (see CLAUDE.md)
|-- requirements-host.txt         # pinned host Python tools
|-- docs/
|   |-- PROJECT_PLAN.md           # this file (given)
|   |-- ENGINEERING_RULES.md      # coding rules (given)
|   |-- PROGRESS.md               # phase status, kept up to date
|   |-- DECISIONS.md              # decisions the plan left open
|   |-- TECHNICAL_REPORT.md       # final plain-language report (Phase 7, updated by extensions)
|   |-- phases/                   # one learning note per phase
|   `-- images/                   # small figures (plots, diagrams), each under 1 MB
|-- driver/                       # the kernel module (GPL-2.0-only)
|   |-- Kbuild
|   |-- Makefile                  # convenience wrapper for out-of-tree builds
|   |-- hvac_temp_regs.h          # register addresses and bit constants
|   |-- hvac_temp_conv.h          # pure static inline conversion functions
|   |-- hvac_temp.h               # private device struct, shared prototypes
|   |-- hvac_temp_main.c          # I2C driver, probe, IIO channel and read_raw
|   |-- hvac_temp_alarm.c         # alarm-limit sysfs attributes
|   `-- hvac_temp_kunit.c         # KUnit tests for the conversion functions
|-- examples/
|   `-- hello_module/             # minimal "hello" kernel module for learning
|-- thermostat/                   # user-space C code (MIT)
|   |-- CMakeLists.txt
|   |-- include/                  # one header per module
|   |-- src/                      # hvac_core.c config.c sensor_iio.c modbus_server.c log.c main.c
|   |-- sim/                      # room_model.c baseline_controller.c sil_main.c
|   |-- tests/                    # Unity unit tests, one file per module
|   |-- config/hvacd.conf         # default configuration
|   `-- third_party/unity/        # vendored Unity test framework (pinned)
|-- br2-external/                 # everything Buildroot-specific
|   |-- external.desc  external.mk  Config.in
|   |-- configs/hvac_qemu_aarch64_defconfig
|   |-- board/hvac-qemu/
|   |   |-- linux.fragment        # kernel config additions
|   |   |-- post-build.sh         # copies guest tests into the image
|   |   `-- rootfs-overlay/       # init scripts, default config
|   `-- package/                  # hvac-temp, hvac-hello, hvacd (Buildroot packages)
|-- tests/
|   |-- common/                   # code shared by host and guest tests (modbus_client.py)
|   |-- guest/                    # pytest suites that run inside QEMU
|   |-- host/                     # pytest suites that run on the host
|   `-- mutation/mutants/         # patches that deliberately break the driver
|-- scripts/                      # check_env, fetch_buildroot, build, run_qemu, test_qemu,
|                                 # test_host, static_analysis, run_mutation, collect_metrics,
|                                 # plot_sil, verify_push, lint, setup_host_venv
|-- results/
|   |-- metrics.json  METRICS.md  # committed summary
|   |-- host/  guest/  sil/       # generated, partly committed (small JSON only)
|   `-- logs/                     # generated, never committed
`-- .github/workflows/ci.yml
```

Extensions add `rtos/` (Ext 4) and new files inside `thermostat/`.

---

## 4. Interfaces and contracts

These are fixed. Code and tests must match them exactly.

### 4.1 The simulated chip (TMP102-compatible register model)

| Register | Name | Meaning | Driver access |
| --- | --- | --- | --- |
| 0x00 | TEMP | Latest temperature | read |
| 0x01 | CONFIG | Configuration | read in probe (presence check) |
| 0x02 | T_LOW | Lower alarm limit | read and write |
| 0x03 | T_HIGH | Upper alarm limit | read and write |

- Each register is 16 bits, sent **most significant byte first** on the wire.
- Temperature format: 12-bit two's complement in bits 15..4; bits 3..0 are ignored. One step = 0.0625 °C.
- Valid physical range: -40 °C to +125 °C (-40000 to 125000 millidegrees).
- The driver uses `i2c_smbus_read_word_swapped()` and `i2c_smbus_write_word_swapped()`.
- `i2c-stub` stores words in SMBus order (least significant byte first). Test helpers therefore always
  think in "chip register value" and byte-swap when calling `i2cset`/`i2cget`
  (example: to store chip value 0x1900, run `i2cset -y BUS 0x48 0x00 0x0019 w`).

### 4.2 Driver contract (`hvac_temp`)

| Item | Value |
| --- | --- |
| Module file | `hvac_temp.ko` (built from `hvac_temp_main.c` + `hvac_temp_alarm.c`) |
| I2C device id | `"hvac_temp"` |
| Device-tree compatible | `"hvac,hvac-temp"` (fictional vendor prefix; record in DECISIONS.md) |
| IIO device name | `hvac_temp` |
| Channels | one `IIO_TEMP` channel, `info_mask_separate = RAW | SCALE` |
| Probe checks | adapter supports `I2C_FUNC_SMBUS_WORD_DATA` (else `-EOPNOTSUPP`); CONFIG register readable |
| Success log | `dev_info(dev, "ready at address 0x%02x\n", client->addr)` |
| Probe-failure log | `dev_err_probe(dev, ret, "chip not responding\n")` |
| `remove()` | none (everything is `devm_`-managed) |
| Kernel module license | `GPL` (`MODULE_LICENSE("GPL")`), SPDX `GPL-2.0-only` |

sysfs files under `/sys/bus/iio/devices/iio:deviceN/` (find N by matching the `name` file):

| File | Mode | Content | Rules |
| --- | --- | --- | --- |
| `name` | r | `hvac_temp` | created by IIO |
| `in_temp_raw` | r | signed integer, -2048..2047 | raw 12-bit value; I2C error returned as the read error |
| `in_temp_scale` | r | `62.500000` | millidegrees per raw step (IIO_VAL_INT_PLUS_MICRO 62, 500000) |
| `alarm_low_millicelsius` | rw | integer millidegrees | write: `kstrtoint`, range -40000..125000 else `-EINVAL`; stored rounded to the nearest 0.0625 °C step; read back rounded to the nearest millidegree |
| `alarm_high_millicelsius` | rw | same | same, register 0x03 |

No check that low < high in the driver (the chip does not require it); document this.

### 4.3 Conversion functions (`driver/hvac_temp_conv.h`)

Pure `static inline` functions, shared by the driver and the KUnit test module. No I/O, no locking.

```c
#define HVAC_TEMP_MIN_MC  (-40000)
#define HVAC_TEMP_MAX_MC  125000

/* 16-bit chip register -> signed raw steps (-2048..2047). */
static inline int hvac_temp_reg_to_raw(u16 reg)
{ return sign_extend32(reg >> 4, 11); }

/* Signed raw steps -> 16-bit chip register (bits 3..0 zero). */
static inline u16 hvac_temp_raw_to_reg(int raw)
{ return (u16)((raw & 0xFFF) << 4); }

/* Raw steps -> millidegrees, rounded to nearest (62.5 m°C per step). */
static inline int hvac_temp_raw_to_mc(int raw)
{ return DIV_ROUND_CLOSEST(raw * 125, 2); }

/* Millidegrees -> raw steps, rounded to nearest. Caller range-checks first. */
static inline int hvac_temp_mc_to_raw(int mc)
{ return DIV_ROUND_CLOSEST(mc * 2, 125); }
```

Reference table (used by KUnit and pytest):

| °C | raw | register | i2cset word (swapped) |
| --- | --- | --- | --- |
| +125 | 2000 | 0x7D00 | 0x007D |
| +25 | 400 | 0x1900 | 0x0019 |
| +0.0625 | 1 | 0x0010 | 0x1000 |
| 0 | 0 | 0x0000 | 0x0000 |
| -0.0625 | -1 | 0xFFF0 | 0xF0FF |
| -25 | -400 | 0xE700 | 0x00E7 |
| -40 | -640 | 0xD800 | 0x00D8 |

### 4.4 Thermostat control core (`thermostat/include/hvac_core.h`)

```c
typedef enum {
	HVAC_STATE_IDLE = 0, HVAC_STATE_COOLING = 1,
	HVAC_STATE_HEATING = 2, HVAC_STATE_FAULT = 3
} hvac_state_t;

typedef struct {
	int32_t  cool_setpoint_mc;   /* default 26000 */
	int32_t  heat_setpoint_mc;   /* default 20000 */
	int32_t  deadband_mc;        /* default 1000  */
	uint32_t min_off_s;          /* default 180   */
	uint32_t min_run_s;          /* default 120   */
	uint32_t fault_recover_count;/* default 5     */
	int32_t  max_step_mc;        /* default 5000  */
} hvac_config_t;

typedef struct { bool read_ok; int32_t temp_mc; uint64_t now_ms; } hvac_input_t;
typedef struct { bool compressor; bool heater; bool fan; bool alarm; } hvac_outputs_t;

int          hvac_config_validate(const hvac_config_t *cfg);        /* 0 or -EINVAL */
int          hvac_core_init(hvac_ctx_t *ctx, const hvac_config_t *cfg);
hvac_state_t hvac_core_step(hvac_ctx_t *ctx, const hvac_input_t *in, hvac_outputs_t *out);
int          hvac_core_set_setpoints(hvac_ctx_t *ctx, int32_t cool_mc, int32_t heat_mc);
uint32_t     hvac_core_compressor_starts(const hvac_ctx_t *ctx);
const char  *hvac_state_name(hvac_state_t state);  /* "IDLE", "COOLING", "HEATING", "FAULT" */
```

`hvac_ctx_t` is a plain struct defined in the header (so callers can allocate it statically) holding the
config, current state, `last_valid_mc`, `has_last_valid`, `good_count`, `compressor_on_since_ms`,
`compressor_off_since_ms`, `compressor_has_run`, `compressor_starts`.

**Validation rules (`hvac_config_validate`, also used for Modbus setpoint writes):**
setpoints within 5000..40000; `deadband_mc` within 100..5000;
`heat_setpoint_mc + deadband_mc < cool_setpoint_mc - deadband_mc`; `min_off_s` and `min_run_s` within 0..3600;
`fault_recover_count` within 1..100; `max_step_mc` within 500..20000.

**Step algorithm** (exactly one transition at most per step; evaluate in this order):

1. **Validity.** A reading is valid if `read_ok`, and `HVAC_TEMP_MIN_MC <= temp_mc <= HVAC_TEMP_MAX_MC`,
   and (no previous valid reading, or `abs(temp_mc - last_valid_mc) <= max_step_mc`).
   A valid reading updates `last_valid_mc`.
2. **Invalid reading, any state other than FAULT:** go to FAULT, `good_count = 0`. If leaving COOLING,
   set `compressor_off_since_ms = now_ms`.
3. **FAULT:** invalid reading -> `good_count = 0`. Valid reading -> `good_count++`; when
   `good_count >= fault_recover_count`, go to IDLE.
4. **IDLE:** if `temp >= cool_sp + deadband` and the compressor may start, go to COOLING
   (`compressor_on_since_ms = now_ms`, `compressor_starts++`, `compressor_has_run = true`).
   Else if `temp <= heat_sp - deadband`, go to HEATING.
   "May start" = `!compressor_has_run` or `now_ms - compressor_off_since_ms >= min_off_s * 1000`.
5. **COOLING:** if `temp <= cool_sp - deadband` and `now_ms - compressor_on_since_ms >= min_run_s * 1000`,
   go to IDLE and set `compressor_off_since_ms = now_ms`.
6. **HEATING:** if `temp >= heat_sp + deadband`, go to IDLE.

Outputs: `compressor = (state == COOLING)`, `heater = (state == HEATING)`, `fan = compressor || heater`,
`alarm = (state == FAULT)`. With defaults: cooling starts at 27 °C, stops at 25 °C; heating starts at
19 °C, stops at 21 °C. Note: the minimum-off timer does not apply at first start-up (no previous run).

### 4.5 Configuration file (`/etc/hvacd.conf`)

Format: one `key = value` per line; `#` starts a comment; blank lines ignored; whitespace trimmed.
Unknown key -> error (catches typos). Missing key -> default. Every value range-checked.
Errors print `config:<line>: <message>` and `hvacd` exits with code 1.

| Key | Default | Meaning |
| --- | --- | --- |
| `cool_setpoint_mc` | 26000 | cooling setpoint |
| `heat_setpoint_mc` | 20000 | heating setpoint |
| `deadband_mc` | 1000 | half-width of the switching band |
| `min_off_s` | 180 | compressor minimum off time |
| `min_run_s` | 120 | compressor minimum run time |
| `fault_recover_count` | 5 | good readings needed to leave FAULT |
| `max_step_mc` | 5000 | largest believable change between readings |
| `loop_period_ms` | 1000 | control loop period (100..10000) |
| `iio_base_dir` | `/sys/bus/iio/devices` | where to look for IIO devices |
| `iio_device_name` | `hvac_temp` | IIO device to use |
| `modbus_port` | 502 | TCP port (1..65535) |
| `modbus_max_clients` | 4 | simultaneous Modbus clients (1..8) |
| `log_level` | `info` | `error`, `warn`, `info`, `debug` |

Extensions add keys (section 8), always with defaults that keep the extension disabled unless noted.

### 4.6 Modbus TCP register map (holding registers, function codes 03, 06, 16)

| Address | Name | Unit / encoding | Access |
| --- | --- | --- | --- |
| 0 | temperature | centidegrees, int16 two's complement (2450 = 24.50 °C) | read |
| 1 | state | 0 IDLE, 1 COOLING, 2 HEATING, 3 FAULT | read |
| 2 | cool setpoint | centidegrees | read/write |
| 3 | heat setpoint | centidegrees | read/write |
| 4 | outputs | bit0 compressor, bit1 heater, bit2 fan | read |
| 5 | alarms | bit0 sensor fault, bit1 rejected setpoint write | read |
| 6 | compressor starts | count, saturates at 65535 | read |
| 7 | uptime | minutes, saturates at 65535 | read |
| 8 | diagnostics | FDD bits (Ext 2); 0 until then | read |
| 9 | protocol version | 1 | read |

Rules: writes are accepted by libmodbus into the register table; on the next loop iteration `hvacd`
validates registers 2 and 3 with `hvac_core_set_setpoints()`. Valid -> applied. Invalid -> both restored to
the previous values and alarm bit1 set (cleared by the next valid write). Writes to read-only registers
are simply overwritten on the next loop iteration. Temperature register holds the last valid reading
(-32768 if none yet).

### 4.7 Daemon behavior (`hvacd`)

- Command line: `hvacd --config PATH`, `hvacd --version`, `hvacd --help`. Runs in the foreground.
- Exit codes: 0 clean shutdown (SIGINT/SIGTERM), 1 configuration or start-up error, 2 fatal runtime error.
- Log lines to stderr: `<ISO-8601 UTC time> hvacd <LEVEL> <message>`.
  State changes are logged as `state IDLE -> COOLING temp=27.06C`.
- Loop: deadlines from `CLOCK_MONOTONIC`; between ticks, `select()` serves Modbus clients; a tick reads the
  sensor, runs `hvac_core_step()`, applies outputs (logged; no real GPIO), updates the register table.
- Sensor discovery: scan `iio_base_dir/*/name` for `iio_device_name`. On any read failure, the reading is
  passed to the core as `read_ok = false` and the device is searched for again on the next tick.
- Scale parsing: `in_temp_scale` parsed as a fixed-point decimal with up to 6 fractional digits into an
  integer (`62.500000` -> 62500000 micro-units). `temp_mc = round(raw x scale_micro / 1000000)`, rounding
  to nearest, ties away from zero, computed in `int64_t`.

### 4.8 Guest test runner contract

- Kernel command line `hvac_autotest=1` makes `/etc/init.d/S99autotest` run `/opt/hvac-tests/run_guest_tests.sh`
  instead of the demo services. Optional `hvac_pytest_k=<expr>` limits tests during development (never in a gate).
- The runner first runs `dmesg -n 1` (kernel messages stop printing to the console, so they cannot break
  the result block), mounts debugfs if needed, then runs
  `python3 -m pytest -p no:cacheprovider -q --junitxml=/tmp/hvac-results/junit.xml /opt/hvac-tests`.
- Tests record metrics with `record_metric(name, value, unit, meaning)` from `hvac_test_lib.py`, which
  writes one JSON file per metric into `/tmp/hvac-results/metrics/`.
- The runner then prints, in this order, each marker on its own line:

```
===HVAC-RESULTS-BEGIN===
{"schema":1,"kernel":"<uname -r>","passed":N,"failed":0,"errors":0,"skipped":0,"duration_s":123.4,
 "tests":[{"name":"test_x","outcome":"passed","duration_s":0.4}],
 "metrics":{"<name>":{"value":1,"unit":"us","meaning":"..."}}}
===HVAC-RESULTS-END===
===HVAC-DMESG-BEGIN===
<full dmesg>
===HVAC-DMESG-END===
===HVAC-AUTOTEST-DONE exit=<pytest exit code>===
```

  then `poweroff -f`.
- `scripts/test_qemu.sh` boots with `-no-reboot`, kernel arguments `panic=5 hvac_autotest=1`, a hard
  `timeout` of 60 minutes, saves the console to `results/logs/qemu-console.log`, extracts
  `results/guest/summary.json`, `results/guest/guest_metrics.json` and `results/logs/guest-dmesg.log`,
  and exits non-zero if a marker is missing, the exit code is non-zero, or failed + errors + skipped > 0.

### 4.9 Metrics file contract

`scripts/collect_metrics.py` merges `results/host/*.json` and `results/guest/*.json` into:

```json
{"schema": 1, "generated_utc": "...", "git_commit": "...",
 "metrics": {"<id>": {"value": 0, "unit": "count", "target": "== 0", "pass": true,
                      "meaning": "plain-English sentence", "source": "phase 4, test_memory.py"}}}
```

and renders `results/METRICS.md` as a table (Metric, Value, Target, Pass, What it means). Metric ids
are listed in section 9.

---

## 5. Build platform (Buildroot and QEMU)

### 5.1 Versions

- **Buildroot:** the newest point release of the newest long-term-support series (`YYYY.02.x`) at the time
  Phase 1 starts. Download the release tarball from `https://buildroot.org/downloads/`, verify its SHA-256
  against the official `.sign` file, extract to `.cache/buildroot-<version>/`. Record version and checksum
  in `VERSIONS.md`.
- **Linux kernel:** the version pinned by that Buildroot release's `qemu_aarch64_virt_defconfig`. Record it.
- **Toolchain:** Buildroot external Bootlin toolchain, AArch64, glibc, "stable" variant
  (`BR2_TOOLCHAIN_EXTERNAL_BOOTLIN`). Faster than building a toolchain. Record its version.
- **Unity** (Phase 5) and **FreeRTOS-Kernel** (Ext 4): newest release tag, recorded with commit SHA.

### 5.2 Build layout

- Buildroot source: `.cache/buildroot-<version>/` (never edited).
- Output: `O=.cache/br-output`. Downloads: `BR2_DL_DIR=.cache/br-dl` (cached in CI).
- External tree: `BR2_EXTERNAL=br2-external`, name `HVAC` in `external.desc`, so packages can use
  `$(BR2_EXTERNAL_HVAC_PATH)`.
- `BR2_CCACHE=y` with `BR2_CCACHE_DIR=.cache/ccache`.
- `scripts/build.sh` sets `PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`, runs the build,
  logs to `results/logs/build.log`, and writes `results/host/build_info.json`
  (Buildroot version, kernel version, toolchain, build duration, kernel image and rootfs sizes).
- Local packages (`hvac-temp`, `hvac-hello`, `hvacd`) use `SITE_METHOD = local`. After changing their sources,
  `build.sh` runs `make <pkg>-rebuild` for each and then `make` to regenerate the image.

### 5.3 Defconfig (`br2-external/configs/hvac_qemu_aarch64_defconfig`)

Start from Buildroot's `qemu_aarch64_virt_defconfig`, then set:

- External Bootlin toolchain (AArch64 glibc stable).
- `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_HVAC_PATH)/board/hvac-qemu/linux.fragment"`
- `BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_HVAC_PATH)/board/hvac-qemu/rootfs-overlay"`
- `BR2_ROOTFS_POST_BUILD_SCRIPT="$(BR2_EXTERNAL_HVAC_PATH)/board/hvac-qemu/post-build.sh"`
- `BR2_TARGET_ROOTFS_EXT2_SIZE="768M"`
- Packages: `i2c-tools`, `python3`, `python-pytest`, `libmodbus`, `hvac-temp`, `hvac-hello`,
  later `hvacd`, `mosquitto` (Ext 3). Verify each exists in the pinned Buildroot (`ls package/<name>`);
  if one is missing, record a decision and use the simplest alternative.
- Save with `make savedefconfig BR2_DEFCONFIG=...` so the file stays minimal.

### 5.4 Kernel configuration fragment (`linux.fragment`)

```
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_I2C=y
CONFIG_I2C_CHARDEV=y
CONFIG_I2C_STUB=m
CONFIG_IIO=y
# CONFIG_SENSORS_TMP102 is not set
CONFIG_DEBUG_FS=y
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
CONFIG_KUNIT=y
CONFIG_KUNIT_DEBUGFS=y
CONFIG_KASAN=y
CONFIG_KASAN_GENERIC=y
CONFIG_DEBUG_KMEMLEAK=y
# CONFIG_DEBUG_KMEMLEAK_AUTO_SCAN is not set
CONFIG_PROVE_LOCKING=y
CONFIG_DEBUG_ATOMIC_SLEEP=y
CONFIG_WATCHDOG=y
CONFIG_SOFT_WATCHDOG=m
# CONFIG_WATCHDOG_NOWAYOUT is not set
```

After the kernel builds, confirm each option took effect (Buildroot can silently drop options with unmet
dependencies): the Phase 1 smoke test reads `/proc/config.gz` inside the guest and checks every line.

### 5.5 Root filesystem overlay and init scripts

| File | Runs when | Does |
| --- | --- | --- |
| `/etc/init.d/S05debugfs` | always | mounts debugfs at `/sys/kernel/debug` if not mounted |
| `/etc/init.d/S90hvac-demo` | not autotest | loads `i2c-stub` (0x48), loads `hvac_temp`, creates the device, sets 25 °C, starts `hvacd` (from Phase 6) |
| `/etc/init.d/S99autotest` | `hvac_autotest=1` | runs the guest test runner, prints results, powers off |
| `/etc/hvacd.conf` | | default configuration (from Phase 6) |

`post-build.sh` copies `tests/guest/` and `tests/common/` into `/opt/hvac-tests/`. Use the
`BR2_EXTERNAL_HVAC_PATH` environment variable to find the repository; verify it is set, else fail loudly.

### 5.6 QEMU command lines

Interactive (`scripts/run_qemu.sh`, `make run`): machine `virt`, CPU `cortex-a53`, 2 CPUs (`-smp 2`, so
concurrency tests run in parallel), 2048 MB RAM (KASAN needs memory), `-nographic`, virtio disk with the
ext4 image, user networking with `hostfwd=tcp:127.0.0.1:5020-:502` (Modbus from the host). Prefer Buildroot's
generated start script as the reference and record any differences.

Autotest (`scripts/test_qemu.sh`): the same plus `-no-reboot`, kernel arguments `panic=5 hvac_autotest=1`,
stdin from `/dev/null`, wrapped in `timeout 60m`.

### 5.7 Host requirements

Ubuntu 22.04 or 24.04 on WSL2, at least 25 GB free disk, 8 GB RAM recommended. Packages (the user
installs them; Claude Code never runs sudo):

```
sudo apt update && sudo apt install -y build-essential git make gcc g++ bison flex bc \
  libncurses-dev libssl-dev libelf-dev rsync cpio unzip wget file patch perl xz-utils bzip2 \
  python3 python3-pip python3-venv qemu-system-arm cmake pkg-config ccache jq \
  libmodbus-dev valgrind cppcheck sparse shellcheck \
  mosquitto mosquitto-clients libmosquitto-dev
```

Host Python tools go in `.venv/` from `requirements-host.txt` (pinned): `pytest`, `pyflakes`, `gcovr`, `matplotlib`.

---

## 6. Test strategy

### 6.1 Test layers

| Layer | Runs on | Tools | What it proves | From phase |
| --- | --- | --- | --- | --- |
| Static analysis | host | `checkpatch.pl`, `sparse`, `cppcheck`, `shellcheck`, `pyflakes` | Style and common bug patterns caught without running code | 0, 4, 5 |
| Platform smoke | guest | pytest | The board boots with every tool and kernel option we need | 1 |
| Kernel unit | guest | KUnit | Conversion math is exactly right for all 4096 raw values | 3 |
| Driver functional | guest | pytest + `i2c-stub` | Readings, scale, alarms, error paths are right | 2, 3 |
| Driver robustness and memory | guest | pytest + KASAN + kmemleak + lockdep | No leaks, no use-after-free, no lock bugs under stress | 4 |
| Mutation | host + guest | patches + full guest run | The tests actually catch real bugs | 4 |
| Core unit | host | Unity + ASan/UBSan + Valgrind + gcovr | Every state-machine rule, boundary and timer | 5 |
| Simulation (SIL) | host | C simulator | Control quality over 24 simulated hours | 5 |
| Daemon integration | host | pytest + fake sysfs + Modbus client | Daemon behavior and memory without QEMU | 6 |
| End to end | guest | pytest | Driver + daemon + Modbus together on the board | 6 |
| Continuous integration | GitHub | all of the above | Anyone's checkout builds and passes | 7 |

### 6.2 Shared test helpers (`tests/guest/hvac_test_lib.py`)

Write these once, in Phase 1 and 2, and reuse them everywhere:

- `run(cmd, check=True)`: run a command, return stdout, raise with stderr on failure.
- `load_stub(addr=0x48)` / `unload_stub()`; `stub_bus()` finds the adapter named `SMBus stub driver`.
- `set_chip_reg(reg, value16)` / `get_chip_reg(reg)`: byte-swap and call `i2cset`/`i2cget`.
- `set_temp_raw(raw)`: writes `hvac_temp_raw_to_reg(raw)` to register 0x00 (Python mirror of the C function).
- `new_device(addr)` / `delete_device(addr)`; `iio_dir()` finds the IIO device by `name`.
- `read_attr(name)` / `write_attr(name, value)` returning `(ok, value_or_errno)`.
- `kernel_log_marker()` writes a unique line to `/dev/kmsg`; `kernel_log_since(marker)` returns later lines;
  `assert_kernel_log_clean(lines)` checks the patterns in ENGINEERING_RULES.md section 7.
- `wait_until(predicate, timeout_s, interval_s=0.05)`.
- `record_metric(name, value, unit, meaning)`.

`tests/guest/conftest.py` provides fixtures: `stub` (i2c-stub loaded), `hvac_device` (stub + module +
device at 0x48, torn down in reverse order even on failure) and an autouse fixture that checks the kernel
log after every test.

---

## 7. Compulsory phases

Every phase: follow the phase loop in CLAUDE.md. "Gate" = commands that must pass with 0 failures and
0 skips before committing. Every phase writes its learning note `docs/phases/phase-NN-<slug>.md`.

### Phase 0: Foundation

**Goal.** A clean repository skeleton, a verified host environment and the project conventions.

Tasks:
1. `git init -b main` if needed. Check `git config user.name` and `user.email`; if missing, ask the user.
2. Create the layout in section 3 (empty folders get a short `README.md` saying what will go there).
3. `.gitignore` (ENGINEERING_RULES.md section 8), `LICENSE`, `THIRD_PARTY.md`, `VERSIONS.md`,
   `requirements-host.txt` (pinned), `docs/DECISIONS.md` and `docs/PROGRESS.md` already exist; keep them.
4. `scripts/check_env.sh`: project path not under `/mnt/`; required commands from section 5.7 present
   (print the exact apt command for anything missing); free disk >= 25 GB; RAM (warn below 8 GB);
   git identity set; `gh auth status` succeeds. Prints a pass/fail table and exits non-zero on any fail.
5. `scripts/setup_host_venv.sh` creates `.venv` from `requirements-host.txt`.
6. `scripts/lint.sh`: `shellcheck` on every shell script, `pyflakes` on every Python file.
7. Top-level `Makefile` with targets `check-env`, `venv`, `lint`, and placeholders for later targets that
   print "added in Phase N" and exit 1 (so nobody mistakes a placeholder for a pass).
8. `README.md`: one paragraph on the project and a "work in progress" note.

Gate: `make check-env && make venv && make lint`.
Learning note topics: repository structure, why pinned versions, what a Makefile target is, git tags.

### Phase 1: Platform (Buildroot + QEMU + automated guest testing)

**Goal.** One command builds an ARM64 Linux image; one command boots it and runs tests automatically.

Tasks:
1. `scripts/fetch_buildroot.sh` (section 5.1), `br2-external/` skeleton, defconfig (5.3), kernel
   fragment (5.4), overlay and init scripts (5.5), `post-build.sh`.
2. `scripts/build.sh`, `scripts/run_qemu.sh`, `scripts/test_qemu.sh` (5.2, 5.6, 4.8).
3. Guest runner `tests/guest/run_guest_tests.sh` and first helpers in `hvac_test_lib.py`, `conftest.py`.
4. `scripts/test_host.sh` (for now: lint) and Makefile targets `build`, `run`, `test-host`, `test-qemu`, `test-all`.
5. `tests/guest/test_smoke.py`:
   - `uname -m` is `aarch64`; kernel version equals the one recorded in `VERSIONS.md`.
   - Every option in `linux.fragment` is set as expected in `/proc/config.gz`.
   - debugfs is mounted; `/sys/kernel/debug/kmemleak` and `/sys/kernel/debug/kunit` exist.
   - `i2c-stub` loads with `chip_addr=0x48`; its bus is found; an `i2cset`/`i2cget` round trip on
     register 0x02 returns the written value; `i2c-stub` unloads cleanly.
   - `python3`, `pytest`, `i2cset`, `i2cget`, `i2cdetect` are present.
   - Kernel log has no errors since boot (apply the clean-log check to the boot log, allowing only
     patterns recorded in DECISIONS.md with a reason).
6. Record boot time to test start as metric `guest_boot_to_tests_s`.

Gate: `make test-all`. The first build takes long: run it in the background and poll the log.
Learning note topics: cross-compilation, Buildroot (and how it maps to Yocto), kernel config, root
filesystem, QEMU, how the autotest markers carry results out of the virtual machine.

### Phase 2: Driver skeleton and hello module

**Goal.** A driver that binds to the simulated chip and proves the chip answers, plus a minimal module
for learning.

Tasks:
1. `examples/hello_module/hello.c` + `Kbuild`: prints `hello: loaded` / `hello: unloaded`. Buildroot package `hvac-hello`.
2. `driver/hvac_temp_regs.h`, `driver/hvac_temp.h` (private struct: `client`, `lock`),
   `driver/hvac_temp_main.c` with `module_i2c_driver()`, ID table, OF table, probe (functionality check,
   `devm_iio_device_alloc` for the private struct, mutex init, CONFIG register read, `dev_info`).
   No IIO registration yet. Buildroot package `hvac-temp` (kernel-module + generic-package).
3. `driver/Makefile` for building against the Buildroot kernel directly (used by static analysis).
4. Tests `tests/guest/test_driver_probe.py`:
   - hello module: load prints `hello: loaded`, unload prints `hello: unloaded`.
   - `modprobe hvac_temp` succeeds and it appears in `/proc/modules`.
   - Device at 0x48 on the stub bus: probe succeeds, log shows `ready at address 0x48`,
     `/sys/bus/i2c/devices/<bus>-0048/driver` links to `hvac_temp`.
   - Device at 0x49 (no simulated chip there): probe fails, `chip not responding` is logged, no driver
     link, no IIO device, kernel log otherwise clean.
   - `delete_device` unbinds cleanly; 50 load/bind/unbind/unload cycles leave a clean log.

Gate: `make test-all`.
Learning note topics: kernel vs user space, modules, bus/device/driver model, probe, `new_device`, device
tree (with the example node from section 4.2), `devm_`, reading `dmesg`.

### Phase 3: Complete driver (IIO, conversion, alarm limits, KUnit)

**Goal.** The driver reports correct temperatures through IIO and manages the alarm limits safely.

Tasks:
1. `driver/hvac_temp_conv.h` exactly as section 4.3.
2. IIO channel and `read_raw` (RAW under the mutex; SCALE constant), `devm_iio_device_register`.
3. `driver/hvac_temp_alarm.c`: the two alarm attributes (section 4.2) via the IIO `attrs` group,
   each read/write under the mutex.
4. `driver/hvac_temp_kunit.c`: suite `hvac_temp_conv` with cases: reference table (4.3), all 4096 raw values
   round-trip `raw -> reg -> raw`, rounding (`mc_to_raw`: 31 -> 0, 32 -> 1, -31 -> 0, -32 -> -1;
   `raw_to_mc`: 1 -> 63, -1 -> -63), extremes (-40000, 125000). Built as a separate module `hvac_temp_kunit.ko` (`obj-$(CONFIG_KUNIT)`).
5. Tests:
   - `test_driver_conversion.py`: parametrized over the reference table: set register, read
     `in_temp_raw`, check raw; check raw x scale equals the expected millidegrees. Includes byte-order
     cases where the two register bytes differ.
   - `test_driver_attributes.py`: `name` is `hvac_temp`; `in_temp_scale` is `62.500000`.
   - `test_driver_alarms.py`: write/read-back for typical values (25000, -10000, 0, 125000, -40000);
     chip register contents checked with `i2cget`; rounding case (writing 62 stores 1 step = 62.5 m°C,
     which reads back as 63; writing 31 stores 0 steps and reads back 0); invalid writes (`abc`, empty, `125001`, `-40001`,
     `99999999999`) each return `EINVAL` and leave the register unchanged.
   - `test_kunit.py`: load `hvac_temp_kunit`, read `/sys/kernel/debug/kunit/hvac_temp_conv/results`, all cases
     pass, case count recorded as metric `kunit_cases_passed`.
6. Document honestly in DECISIONS.md and the learning note: `i2c-stub` cannot inject a read error after a
   successful probe, so the read-error path is covered by the missing-chip test, the remove-while-reading
   test (Phase 4) and code review.

Gate: `make test-all`.
Learning note topics: registers, I2C/SMBus transactions, byte order, two's complement, IIO raw and scale,
sysfs input validation, why no floating point in the kernel, KUnit.

### Phase 4: Driver hardening (robustness, memory safety, static analysis, mutation testing)

**Goal.** Evidence that the driver is memory safe, concurrency safe and that the tests catch real bugs.

Tasks:
1. `tests/guest/test_driver_robustness.py`:
   - Concurrent reads: 8 threads x 1000 reads of `in_temp_raw`; 0 errors; every value equals the set value.
   - Concurrent reads and alarm writes: 4 reader threads and 2 writer threads for 2000 operations;
     0 unexpected errors; final registers equal the last written values.
   - Load/unload stress: `HVAC_STRESS_CYCLES` (default 500) cycles of load module, create device, read,
     delete device, unload module. Metric `driver_load_unload_cycles`.
   - Remove while reading: one thread reads in a loop while the main thread deletes the device; reads
     either succeed or fail with `ENODEV`/`EIO`/`ENOENT`; then re-create the device and read correctly.
2. `tests/guest/test_memory.py`:
   - kmemleak: `echo clear`, run 200 cycles, `echo scan` twice (reduces false positives), read the report,
     count entries that mention `hvac_temp`. Must be 0. Metric `kmemleak_reports`.
   - KASAN and lockdep are enabled (`/proc/config.gz`); a kernel-log scan over the whole run finds no
     `KASAN` or lockdep reports. Metrics `kasan_reports`, `lockdep_reports`.
   - Read latency: 1000 reads of `in_temp_raw` timed with `time.perf_counter_ns()`; metrics
     `driver_read_latency_median_us` and `driver_read_latency_p99_us` (emulated board, includes Python
     overhead: relative numbers only, say so in the meaning text).
3. `scripts/static_analysis.sh`:
   - `checkpatch.pl --no-tree --file --terse --show-types` from the Buildroot kernel source on every
     `driver/` file; 0 errors; 0 warnings except types listed in an `ALLOWED_TYPES` variable, each justified
     in DECISIONS.md (for example the undocumented device-tree compatible string of an out-of-tree driver).
   - `sparse` via the kernel build (`make ... C=2`) in a scratch copy of `driver/` under `results/tmp/`; 0 warnings.
   - Writes `results/host/static_analysis.json`. Called by `make test-qemu` (needs the kernel tree).
4. Mutation testing: `tests/mutation/mutants/` with three patches against `driver/`:
   `01-no-sign-extension.patch` (raw = `reg >> 4` only), `02-no-byte-swap.patch` (plain word read/write),
   `03-no-range-check.patch` (alarm write accepts any value). `scripts/run_mutation.sh` applies each in
   turn, rebuilds the driver and image, runs the guest tests, expects a failure, and always restores the
   original source (`trap`). Writes `results/host/mutation.json`; metric `mutation_score` = killed / total.
   Target 3/3. Document why a "remove the mutex" mutant is not included (race bugs are not reliably
   reproducible, so a test cannot be guaranteed to catch it). `make mutation` runs it; it is part of the
   Phase 4 and Phase 7 gates only (slow).

Gate: `make test-all && make mutation`.
Learning note topics: race conditions and mutexes vs spinlocks, KASAN, kmemleak, lockdep, use-after-free,
static analysis, what mutation testing proves.

### Phase 5: Thermostat control core and simulation

**Goal.** A pure, fully tested control core and numbers that show it controls a room well.

Tasks:
1. `thermostat/` CMake project (C11, flags from ENGINEERING_RULES.md 4.1). Targets: `hvac_core` (static
   library: `hvac_core.c`, `config.c`), unit-test executables, `sil` simulator. Options `HVAC_SANITIZE`
   (ASan + UBSan) and `HVAC_COVERAGE` (gcov).
2. Vendor Unity (pinned release) into `thermostat/third_party/unity/` with its license.
3. `hvac_core.c` exactly as section 4.4. `config.c`: `config_parse_string(text, &cfg, err, err_len)`
   (pure, testable) and `config_load_file(path, ...)` (reads the file into a fixed buffer, max 8 KB, then
   calls the parser), section 4.5.
4. Unit tests:
   - `test_hvac_core.c`: every transition in 4.4 including exact boundaries (`>=`/`<=`, one millidegree
     either side), minimum run and off timers using fake time (no sleeping), first start allowed
     immediately, entry to FAULT from each state, recovery after exactly `fault_recover_count` good
     readings, implausible jump detection, setpoint validation, compressor start counting, outputs mapping.
   - `test_config.c`: valid file, comments and blank lines, unknown key, bad number, out of range,
     missing key uses default, cross-field rule (heating band below cooling band), line numbers in errors.
5. Room simulator (`thermostat/sim/`), integer arithmetic in micro-degrees (`int64_t`), 1-second steps:
   - Room: `dT = (T_out - T) x leak_ppm / 1e6 + gains`, with `leak_ppm = 278` (about 1-hour time constant),
     cooling `-6000` µ°C/s when the compressor is on, heating `+6000` µ°C/s when the heater is on, solar gain
     (half-sine from 06:00 to 18:00, peak `+1500` µ°C/s at noon, hot and mild days only) and occupancy gain
     (`+300` µ°C/s, 09:00-18:00, every day). Simulation starts at 00:00.
   - Sensor: true temperature plus seeded noise (xorshift32, uniform within ±100 m°C), quantized down to
     0.0625 °C steps like the chip.
   - Scenarios (24 h each): `hot_day` (outside 35 °C, start 30 °C), `cold_day` (outside 8 °C, start 14 °C),
     `mild_day` (outside 24 °C, start 24 °C).
   - Controllers: the product `hvac_core` with defaults, and a naive `baseline_controller.c` (sim only):
     cooling on above 26 °C, off below 26 °C, heating mirror at 20 °C, no deadband, no timers.
   - Output per run: CSV every 10 s to `results/sil/<scenario>_<controller>.csv` (git-ignored) and metrics to
     `results/sil/sil_metrics.json` (committed).
   - Metrics per run: `comfort_pct` (share of time the true room temperature is within the comfort band
     18.5..27.5 °C, i.e. the four switching thresholds plus the usual ±0.5 °C comfort tolerance, counted
     after the room first enters the band),
     `time_to_comfort_min`, `compressor_starts_per_hour`, `short_cycle_violations` (starts with the preceding
     off gap below `min_off_s`), `min_off_gap_s`, `compressor_duty_pct`, `max_overshoot_mc`.
   - Assertions (in `scripts/test_host.sh` via a small checker): product `short_cycle_violations == 0`;
     product `comfort_pct >= 98` in every scenario; product starts per hour lower than baseline in `hot_day`.
6. `scripts/plot_sil.py` (host venv): `docs/images/sil_hot_day.png`, first 4 simulated hours, two panels
   (product vs baseline): temperature, the four thresholds, compressor on/off shading.
7. `scripts/test_host.sh` (creates `.venv` first if missing, so CI can call it directly) now runs: lint,
   `cppcheck --enable=warning,style,performance,portability --error-exitcode=1` on `thermostat/`
   (excluding `third_party/`), unit tests (normal), unit tests (sanitizers), unit tests under
   `valgrind --leak-check=full --errors-for-leak-kinds=definite,indirect --error-exitcode=1`, coverage with
   gcovr, SIL runs and checks. Each step writes a small JSON file to `results/host/`.

Coverage targets: `hvac_core.c` >= 95% lines and >= 90% branches; `config.c` >= 90% lines.
Gate: `make test-all`.
Learning note topics: state machines, hysteresis, compressor short-cycle protection, why the core is pure,
unit tests with fake time, sanitizers and Valgrind, coverage, software-in-the-loop simulation, reading the plot.

### Phase 6: Thermostat daemon on the board (sensor, Modbus, end to end)

**Goal.** `hvacd` runs on the emulated board, reads the real driver, controls, and speaks Modbus.

Tasks:
1. Modules (one header each in `include/`): `log.c`, `sensor_iio.c` (discovery, raw and scale reading,
   fixed-point scale parsing per 4.7), `modbus_server.c` (libmodbus TCP server, up to `modbus_max_clients`,
   register-table sync per 4.6), `main.c` (arguments, signals, loop per 4.7). Every module stays under 400 lines.
2. Unit tests: `test_sensor_iio.c` with a temporary fake sysfs directory (discovery, scale parsing including
   `62.500000`, `1.000000`, `0.5`, malformed values, rounding of negative values, missing files).
3. Buildroot package `hvacd` (cmake-package, depends on libmodbus), default `/etc/hvacd.conf`, demo init
   script `S90hvac-demo` (section 5.5) logging to `/var/log/hvacd.log`.
4. `tests/common/modbus_client.py`: minimal Modbus TCP client using only the standard library
   (read holding registers, write single register, write multiple registers), with a small CLI
   (`python3 tests/common/modbus_client.py --host 127.0.0.1 --port 5020 read 0 10`).
5. Host integration `tests/host/test_daemon_host.py`: fake sysfs tree in a temp dir, `hvacd` on port 15020
   with short timers (`min_off_s = 2`, `min_run_s = 2`, `fault_recover_count = 3`, `loop_period_ms = 200`):
   temperature register matches the fake file; state follows temperature changes; valid setpoint write is
   applied; invalid write is reverted and sets alarm bit1; removing the fake device gives FAULT and alarm
   bit0 within 3 loop periods; restoring it returns to IDLE after 3 good readings; SIGTERM gives exit code 0
   within 2 s. A second test runs the same short scenario under Valgrind: 0 errors, 0 definite or indirect leaks.
6. Guest end to end `tests/guest/test_daemon_e2e.py` (same short-timer config, real driver via `i2c-stub`):
   the same checks as the host test, plus: `hvacd` exits with code 1 and a clear message on a bad config;
   `--version` works. Metrics: `modbus_roundtrip_median_ms` (100 reads), `fault_detection_latency_s`
   (device deleted -> FAULT visible over Modbus), `hvacd_rss_kb` (`VmRSS` after 30 s), `hvacd_binary_bytes`
   (stripped size).

Gate: `make test-all`.
Learning note topics: user space reading sysfs, the select() loop, Modbus TCP (client/server, holding
registers, function codes), why setpoints are validated, fake-filesystem testing, end-to-end testing.

### Phase 7: Continuous integration, metrics, documentation, publishing

**Goal.** A public, reproducible repository whose CI is green and whose documents explain everything.

Tasks:
1. `.github/workflows/ci.yml`, triggers `push`, `pull_request`, `workflow_dispatch`:
   - Job `host` (ubuntu-24.04): apt packages, venv, `make test-host`, upload `results/host` as an artifact.
   - Job `qemu` (needs `host`, `timeout-minutes: 300`): free disk space first (remove preinstalled
     toolchains such as `/usr/share/dotnet`, `/usr/local/lib/android`, `/opt/ghc`), cache `.cache/br-dl`
     and `.cache/ccache` keyed on `VERSIONS.md` and the defconfig, `make test-qemu`, upload `results/`
     (logs included) as an artifact even on failure.
   - Mutation testing is not run in CI (too slow); say so in the README.
2. `scripts/collect_metrics.py` and `make metrics` (sections 4.9 and 9). Every metric has a plain meaning.
3. `README.md` (section 10.1), `docs/TECHNICAL_REPORT.md` (section 10.2), update `THIRD_PARTY.md`, `VERSIONS.md`.
4. Run the full gate including mutation, regenerate metrics, check every target in section 9 is met.
5. Commit `phase 7: ...`, tag `phase-7`, then publish and verify (section 11). Wait for CI to finish;
   if it fails, fix, commit `ci: <what was fixed>`, push, verify again (stuck rule applies).
6. Create release `v1.0.0` with `gh release create v1.0.0 --title "v1.0.0: core project" --notes-file <file>`
   where the notes summarize features and the metrics table.

Gate: `make test-all && make mutation && make metrics`, then a green CI run on the pushed commit.
Learning note topics: continuous integration, caching, artifacts, reproducibility, how to read the metrics.

---

## 8. Extension phases

Each extension is small, uses the same gate (`make test-all`), adds tests, metrics and a learning note
(`docs/phases/ext-0N-<slug>.md`), updates README, TECHNICAL_REPORT (extensions section) and METRICS, then
commits `ext N: <title>`, tags `ext-N`, pushes and verifies (section 11). Do them in this order.

### Ext 1: Watchdog supervision

**Why:** an unattended HVAC controller must recover by itself if its software hangs. A watchdog timer
resets the system unless the software regularly proves it is alive. It is a core embedded concept.

Tasks:
1. `thermostat/src/watchdog.c` + header: `wdt_open(path, timeout_s)` (sets the timeout with
   `WDIOC_SETTIMEOUT`), `wdt_kick()`, `wdt_close_clean()` (writes the magic character `V`, then closes so
   the kernel disarms the timer). Config keys `watchdog_enabled` (default 0; 1 in the demo config),
   `watchdog_device` (`/dev/watchdog`), `watchdog_timeout_s` (default 10, range 2..60).
2. `hvacd` kicks the watchdog once per loop, only after a successful control step. A hung loop therefore
   stops the kicks.
3. Guest tests `test_watchdog.py` with `softdog` loaded as `soft_noboot=1` (it logs instead of rebooting):
   - Healthy daemon for 2.5 x timeout: no expiry message.
   - Daemon frozen with `SIGSTOP`: expiry message appears within timeout + 3 s. Metric
     `watchdog_trigger_latency_s`. Read the exact message text from the pinned kernel's
     `drivers/watchdog/softdog.c`; never assume it.
   - Clean `SIGTERM`: magic close, no expiry message during the next timeout + 3 s.
4. Host unit test: bad device path gives a clear start-up error and exit code 1.

Learning note topics: watchdog timers, magic close, `NOWAYOUT`, why kick only after real work.

### Ext 2: Fault detection and diagnostics (FDD)

**Why:** Carrier's connected-equipment platforms detect equipment problems from data before customers
notice them. This adds simple, explainable diagnostic rules and measures how well they work.

Tasks:
1. `thermostat/src/hvac_fdd.c` + header, pure like `hvac_core.c`:
   - `FDD_COOLING_INEFFECTIVE` (bit0): compressor commanded on continuously; every
     `fdd_ineffective_window_s` (default 1200) the detector compares the temperature now with the reference
     taken at the start of that window; a drop smaller than `fdd_ineffective_min_drop_mc` (default 500)
     sets the bit. Reference resets when the compressor turns off.
   - `FDD_SENSOR_STUCK` (bit1): the valid reading has not changed at all for `fdd_stuck_window_s` (default
     1800). Assumption to document: a real room never stays constant to 1/16 °C for 30 minutes.
   - Bits latch until `hvacd` restarts (keep it simple; document it).
   Config key `fdd_enabled` (default 1) plus the three thresholds above.
2. `hvacd` publishes the bits in Modbus register 8 and logs the first time each bit is set.
3. Simulator fault injection (from hour 6): `capacity_loss` (cooling at 40% strength), `compressor_fail`
   (cooling command has no effect), `sensor_stuck` (sensor output frozen).
   Detection matrix: `capacity_loss@hot_day`, `compressor_fail@hot_day`, `sensor_stuck@hot_day`,
   `sensor_stuck@cold_day`, `sensor_stuck@mild_day`. False-alarm matrix: the three fault-free scenarios
   x 5 seeds. A fault counts as detected when its expected bit is set (`capacity_loss` and
   `compressor_fail` -> bit0, `sensor_stuck` -> bit1); other bits set at the same time are reported, not
   counted against it. Explain in the report that a fault can only be detected when the affected part is
   used (a cooling fault on a day without cooling demand stays hidden).
4. Unit tests for each detector at and around its thresholds, and reset behavior.
5. Metrics: `fdd_detection_rate` (target 100% on the matrix), `fdd_false_alarms` (target 0),
   `fdd_mean_time_to_detect_min`.

Learning note topics: what FDD is, rule-based vs machine-learning diagnostics (mention the public LBNL
FDD datasets as a next step), detection rate, false alarms, time to detect.

### Ext 3: MQTT telemetry

**Why:** connected HVAC and cold-chain products send equipment data to the cloud; MQTT is the usual
lightweight protocol. The key engineering rule: network problems must never disturb control.

Tasks:
1. `thermostat/src/telemetry_mqtt.c` + header using libmosquitto, single-threaded: call
   `mosquitto_loop(mosq, 0, 1)` (non-blocking) each loop iteration; reconnect with a fixed back-off of
   5 s when disconnected. Pure function `telemetry_format_json(const telemetry_t *t, char *buf, size_t len)`
   builds the JSON with `snprintf` into a fixed 512-byte buffer and reports truncation.
2. Topic `hvac/<device_id>/telemetry`, QoS 1, every `mqtt_period_s`. Payload fields: `device`, `seq`,
   `uptime_s`, `temp_c` (two decimals), `state` (name), `compressor`, `heater`, `fan`, `alarms`, `fdd`,
   `compressor_starts`. Status topic `hvac/<device_id>/status`: retained `online` on connect, last-will
   `offline`.
3. Config keys: `mqtt_enabled` (default 0), `mqtt_host` (`127.0.0.1`), `mqtt_port` (1883),
   `mqtt_device_id` (`hvac-01`, letters, digits and `-` only, max 32), `mqtt_period_s` (5, range 1..3600).
4. Buildroot: add `mosquitto` (broker and clients). Host tests start their own broker on port 18830.
5. Tests: JSON formatter unit tests (values, negative temperatures, truncation); guest tests: messages
   arrive at the configured period (within ±20%), payload matches the Modbus registers, `kill -9` of `hvacd`
   produces `offline` on the status topic, stopping the broker does not change the loop period by more than
   100 ms and Modbus keeps answering, restarting the broker leads to reconnection within 15 s.
6. Metrics: `mqtt_messages_received_ratio`, `mqtt_reconnect_s`, `control_loop_jitter_broker_down_ms`.

Learning note topics: publish/subscribe, QoS, retained messages, last will, why control and telemetry are
decoupled.

### Ext 4: The same control core on FreeRTOS

**Why:** Carrier's embedded platform spans embedded Linux and FreeRTOS microcontrollers. Running the
unchanged `hvac_core.c` on FreeRTOS shows portable design and RTOS fundamentals without needing hardware.

Tasks:
1. `scripts/fetch_freertos.sh`: FreeRTOS-Kernel at a pinned release into `.cache/`. Use the official
   POSIX/Linux simulator port (`portable/ThirdParty/GCC/Posix`), which runs FreeRTOS as a normal Linux program.
2. `rtos/` CMake project: `FreeRTOSConfig.h`, `main.c`, `plant_task.c` (room model + sensor, reusing
   `thermostat/sim/room_model.c`), `control_task.c` (calls `hvac_core_step()` unchanged), `logger_task.c`
   (lowest priority). Source files are referenced from `thermostat/`, never copied.
3. Lockstep design for determinism: 1 tick = 1 simulated second. Each tick the plant task sends the
   reading to the control task through a queue, blocks until the outputs come back on a second queue, then
   advances the room. `vTaskDelayUntil()` paces the plant task. Application tasks and queues use static
   allocation (`xTaskCreateStatic`, `xQueueCreateStatic`); the kernel heap uses `heap_4`.
4. Stop after the scenario length: use `vTaskEndScheduler()` if the pinned POSIX port supports it,
   otherwise the plant task flushes output and calls `exit(0)`; record which in DECISIONS.md.
5. Equivalence test: `hot_day`, 2 simulated hours, seed 1, run on the host simulator and on FreeRTOS, each
   writing a per-second trace (`t_s,temp_meas_mc,state,compressor,heater`) with the same shared function.
   The two files must be byte-identical.
6. Metrics: `rtos_trace_match_pct` (100), `rtos_stack_high_water_words` per task, `rtos_min_free_heap_bytes`,
   `rtos_queue_send_failures` (0). Sanitizers and Valgrind are not run on the FreeRTOS binary (the POSIX port
   uses signals and threads internally); say so.

Learning note topics: RTOS tasks, priorities, queues, `vTaskDelayUntil`, static allocation, stack
high-water marks, why a pure core ports without changes.

After Ext 4: update README and TECHNICAL_REPORT, regenerate metrics, tag and release `v1.1.0`.

---

## 9. Metrics catalogue

`collect_metrics.py` must produce every metric below (extension metrics once that extension is done).
"Meaning" must appear in METRICS.md in plain English, close to the text here.

| Id | Unit | Target | Meaning |
| --- | --- | --- | --- |
| `guest_tests_passed` / `guest_tests_failed` | count | failed = 0 | In-guest tests run on the emulated board |
| `host_tests_passed` / `host_tests_failed` | count | failed = 0 | Unit, integration and simulation checks on the host |
| `kunit_cases_passed` | count | all | Kernel-level checks of the conversion math |
| `driver_load_unload_cycles` | count | >= 500 | Times the driver was loaded and removed without a problem |
| `kmemleak_reports` | count | 0 | Memory blocks our driver allocated and never freed |
| `kasan_reports` | count | 0 | Invalid memory accesses (use-after-free, out of bounds) |
| `lockdep_reports` | count | 0 | Locking mistakes that could deadlock |
| `concurrent_read_errors` | count | 0 | Wrong or failed readings with 8 threads reading at once |
| `driver_read_latency_median_us`, `_p99_us` | µs | report | Time for one temperature read on the emulated board (relative only) |
| `checkpatch_errors` / `checkpatch_warnings` | count | 0 / 0 (allowed types excluded) | Kernel coding-style problems |
| `sparse_warnings` | count | 0 | Kernel type-checking problems |
| `cppcheck_findings` | count | 0 | Likely bugs in user-space C found without running it |
| `mutation_score` | killed/total | 3/3 | Deliberately inserted bugs that the tests caught |
| `core_line_coverage_pct` / `core_branch_coverage_pct` | % | >= 95 / >= 90 | Share of control-core code exercised by unit tests |
| `sanitizer_errors`, `valgrind_errors`, `valgrind_leaked_bytes` | count/bytes | 0 | Memory errors and leaks in user-space code |
| `sil_comfort_pct_<scenario>` | % | >= 98 | Share of time the simulated room stayed within 18.5..27.5 °C |
| `sil_starts_per_hour_<scenario>` (product and baseline) | per hour | product < baseline | How often the compressor starts (fewer is gentler) |
| `sil_short_cycle_violations_<scenario>` | count | 0 | Compressor restarts that came too soon |
| `modbus_roundtrip_median_ms` | ms | report | Time for a Modbus request and reply on the emulated board |
| `fault_detection_latency_s` | s | <= 3 x loop period + 1 | Time from sensor loss to alarm visible over Modbus |
| `hvacd_rss_kb`, `hvacd_binary_bytes` | kB / bytes | report | Daemon memory use and program size |
| `guest_boot_to_tests_s`, `build_duration_min`, `rootfs_size_mb` | s / min / MB | report | Platform facts for reproducing the project |
| Extension metrics | | see section 8 | |

---

## 10. Documentation deliverables

### 10.1 README.md (about one screen plus tables)

1. Title, CI badge, one-sentence summary.
2. What this is and why it matters (three to five sentences, Carrier/HVAC context without naming the internship).
3. Architecture diagram (from section 2.1, simplified).
4. Quick start: clone, `make check-env` (prints the apt command if anything is missing), `make test-host`,
   `make test-qemu` (first time 30-90 minutes), `make run`, and how to read Modbus from the host.
5. Results: key metrics table (8 to 12 rows, linked to `results/METRICS.md`) and the SIL plot.
6. Repository map (top-level folders, one line each).
7. Documentation links: technical report, learning notes by phase, decisions, plan.
8. Limitations (emulated hardware, timing numbers relative) and license.

### 10.2 docs/TECHNICAL_REPORT.md

Audience: an engineer or recruiter with no embedded-software background. Length: 3,000 to 4,500 words
plus tables and figures. Concise yet complete. Every acronym expanded on first use, every metric explained.

Outline:
1. Summary (one paragraph: what was built, how it was tested, headline numbers).
2. The problem in everyday terms (a thermostat, a sensor chip, why software between them must be reliable).
3. The system at a glance (diagram and the "one reading, start to finish" story).
4. Components explained simply: driver, simulated chip and tests, thermostat core, daemon and Modbus, build
   platform. For each: what it does, the one or two ideas that make it work, an analogy where helpful.
5. How we know it works: the test layers table and what each layer would catch.
6. Results: the full metrics table with meanings, the SIL plot and a short interpretation.
7. Safety and memory handling: the rules followed and the evidence (KASAN, kmemleak, lockdep, sanitizers,
   Valgrind, mutation score).
8. Extensions (added by each extension phase).
9. How to reproduce (commands and expected run times).
10. Limitations and honest caveats (emulation, no real chip, relative timings, rule-based FDD).
11. Glossary (every technical term used, one line each).

### 10.3 Learning note per phase (`docs/phases/phase-NN-<slug>.md`, 1 to 3 pages)

1. What this phase adds (two or three sentences).
2. Why it matters (link to real HVAC or embedded practice).
3. Files added or changed (table: file, one-line purpose).
4. How it works (numbered steps; a small ASCII diagram if useful).
5. Run it yourself (exact commands and what you should see).
6. Tests and results (table: test, what it checks, result).
7. Key concepts (each in two or three sentences).
8. What could go wrong (pitfalls met or avoided).
9. Interview questions this phase prepares you for (three to five, with short answers).

### 10.4 PROGRESS.md and DECISIONS.md

Keep the formats already in those files. PROGRESS.md must always let a new session continue the work
without reading the chat history.

---

## 11. Publishing to GitHub and verifying the push

Used at Phase 7 and after every extension.

1. Pre-push checks: `git status` clean; no file over 5 MB in history
   (`git rev-list --objects --all` with sizes); no secrets (search for `token`, `ghp_`, `BEGIN PRIVATE KEY`,
   `password`); `.cache/` and `results/logs/` not tracked.
2. Create the repository once:
   `gh repo create hvac-linux-driver --public --description "<from CLAUDE.md>" --source=. --remote=origin`
   (if it already exists, add it as `origin` instead).
3. `git push -u origin main` and `git push origin --tags`.
4. `scripts/verify_push.sh` (exits non-zero on any mismatch):
   - `git fetch origin --tags`;
   - `git rev-parse main` equals `git rev-parse origin/main`;
   - `git rev-list --count main` equals `git rev-list --count origin/main`;
   - every local tag exists on the remote with the same commit (`git ls-remote --tags origin`);
   - prints the repository URL from `gh repo view --json url`.
5. CI: find the run for the pushed commit (`gh run list --commit <sha>`) and wait for it
   (`gh run watch <id> --exit-status`). The QEMU job can take hours on the first run; keep polling.
6. Record the repository URL, pushed commit, tags and CI result in PROGRESS.md.

---

## 12. Known pitfalls

| Pitfall | What to do |
| --- | --- |
| WSL `PATH` contains Windows folders with spaces; Buildroot refuses to build | `build.sh` sets a clean `PATH` |
| Building under `/mnt/c` is slow and breaks permissions | Work only under `/home/priyanshu/...` |
| Local-package source changes not picked up | `make <pkg>-rebuild` then `make` (build.sh does it) |
| `i2c-stub` byte order | Helpers always byte-swap (section 4.1) |
| I2C bus and IIO device numbers change between boots | Find them by name, never hard-code |
| kmemleak false positives from boot | Clear before, scan twice after, count only `hvac_temp` entries |
| Kernel messages mixed into the result block | `dmesg -n 1` before tests; full dmesg printed separately |
| Kernel panic hangs the test run | `panic=5` + `-no-reboot` + `timeout` |
| Floating point in kernel code | Never; integer math only |
| `devm_mutex_init` may not exist in the pinned kernel | Check the header; fall back to `mutex_init` and record it |
| checkpatch flags the out-of-tree device-tree compatible | Allow that type explicitly, with reason in DECISIONS.md |
| libmodbus applies writes before we can check them | Validate on the next loop and restore (section 4.6) |
| Port 502 needs root on the host | Host tests use port 15020; guest uses 502 |
| GitHub runner disk is small | Free space step before the QEMU job |
| Commands longer than a few minutes | Run in the background with a log, poll until finished |
| `make` warns about clock skew after Windows sleep | Ask the user to run `sudo hwclock -s` |
| Buildroot option silently dropped | Smoke test checks every fragment option in `/proc/config.gz` |
