# Decisions

Every choice the project plan leaves open is recorded here before the code relies on it. This keeps the
project explainable: a reader can see what was decided, why, and what else was considered.

Format (newest at the bottom):

```
## D-NNN: <short title>
- Date / phase:
- Context: what question came up
- Decision: what was chosen
- Reason: why (evidence, links to logs or docs)
- Alternatives considered:
- Consequences: what this affects, anything to revisit
```

## D-001: Plan documents are the source of truth
- Date / phase: project start
- Context: an earlier study guide ("Carrier Internship Prep: Project Guide") describes the same project
  more loosely.
- Decision: `docs/PROJECT_PLAN.md` and `docs/ENGINEERING_RULES.md` win wherever the two differ.
- Reason: the plan fixes exact interfaces, units and tests, which the guide does not.
- Alternatives considered: none.
- Consequences: example code in the guide (for example a 32-bit time field) is superseded by the plan.

## D-002: Repository root is `claude-code-kickoff/`
- Date / phase: 2026-11-15, Phase 0
- Context: CLAUDE.md names the project root as `/home/priyanshu/projects/linux-driver-project`, but the
  given files (CLAUDE.md, docs/) live in `/home/priyanshu/projects/linux-driver-project/claude-code-kickoff`.
- Decision: the git repository root is `.../linux-driver-project/claude-code-kickoff`, where CLAUDE.md is.
- Reason: section 3 of the plan puts CLAUDE.md at the repository root; moving the user's files is riskier
  than using them where they are. All scripts find the root relative to themselves, never by absolute path.
- Alternatives considered: moving everything one level up.
- Consequences: none for the code. The rules "not under /mnt" and "under /home" still hold.

## D-003: Host Ubuntu 26.04 accepted
- Date / phase: 2026-11-15, Phase 0
- Context: the plan lists Ubuntu 22.04 or 24.04; this host runs Ubuntu 26.04 LTS.
- Decision: `check_env.sh` checks tools and resources, not the release number.
- Reason: what matters is that the tools exist; all of them are installed. CI uses ubuntu-24.04 as planned.
- Alternatives considered: failing on an unlisted release (would block for no technical reason).
- Consequences: host and CI tool versions may differ slightly (for example cppcheck); both must be clean.

## D-004: Host Python requirements pin every package, including dependencies
- Date / phase: 2026-11-15, Phase 0
- Context: the plan pins pytest, pyflakes, gcovr and matplotlib but says nothing about their dependencies.
- Decision: `requirements-host.txt` is the full `pip freeze` of a clean install of pytest 9.2.0,
  pyflakes 4.1.0, gcovr 8.8 and matplotlib 3.11.2.
- Reason: "pinned versions only" applies to every download; pinning only the top level would let
  dependencies drift.
- Alternatives considered: top-level pins only; a lock tool (extra dependency).
- Consequences: upgrading a tool means regenerating the whole file.

## D-005: Windows `Zone.Identifier` files are git-ignored
- Date / phase: 2026-11-15, Phase 0
- Context: the given files arrived with `*:Zone.Identifier` companions (Windows download metadata).
- Decision: ignore them in `.gitignore`; do not delete the user's files.
- Reason: they carry no project content, and a colon in a file name breaks checkouts on Windows.
- Alternatives considered: deleting them.
- Consequences: none.

## D-006: Empty sub-folders hold `.gitkeep`; their parent README describes them
- Date / phase: 2026-11-15, Phase 0
- Context: git does not store empty folders; the plan asks for a short README in empty folders.
- Decision: each top-level area (`driver/`, `thermostat/`, `tests/`, `br2-external/`, `results/`,
  `examples/hello_module/`, `docs/phases/`, `docs/images/`) has a README describing its sub-folders;
  deeper empty sub-folders hold an empty `.gitkeep`. Each `.gitkeep` is removed once real files arrive.
- Reason: avoids a dozen near-identical one-line READMEs while keeping the layout explained.
- Alternatives considered: a README in every empty folder.
- Consequences: none.

## D-007: Buildroot 2026.02.3, checksum verified against the `.sign` file without GPG
- Date / phase: 2026-11-15, Phase 1
- Context: the plan asks for the newest point release of the newest LTS series and a SHA-256 check
  against the official `.sign` file.
- Decision: Buildroot 2026.02.3 (newest `2026.02.x` on buildroot.org on 2026-11-15). `fetch_buildroot.sh`
  requires the download's SHA-256 to equal both the value pinned in `scripts/common.sh` and the value in
  the `.sign` file. The PGP signature itself is not verified.
- Reason: the pinned value protects against a changed download even if the server were compromised;
  the `.sign` comparison satisfies the plan. GPG verification would need the maintainer's key fetched from
  a third site, adding a moving part with little extra protection once the checksum is pinned.
- Alternatives considered: full `gpg --verify` with a pinned key file.
- Consequences: upgrading Buildroot means updating `scripts/common.sh` and `VERSIONS.md` together.

## D-008: Defconfig differences from Buildroot's `qemu_aarch64_virt_defconfig`
- Date / phase: 2026-11-15, Phase 1
- Context: the plan says "start from `qemu_aarch64_virt_defconfig`, then set ...".
- Decision: kept the kernel version (6.18.7), kernel config file, patch dir, hash checking, DHCP on eth0
  and ext4 rootfs. Changed: Bootlin external toolchain instead of the internal one (so the
  `HOST_LINUX_HEADERS_CUSTOM_6_18` line is dropped: the toolchain brings its own headers); removed
  `BR2_PACKAGE_HOST_QEMU` (QEMU comes from the host package `qemu-system-arm`; building it would add
  time for nothing); removed the post-image script (it only writes a `start-qemu.sh` for Buildroot's own
  defconfig names and does nothing for ours). Added `BR2_PACKAGE_PYTHON3_PYEXPAT` (XML parsing for the
  JUnit report) and `BR2_PACKAGE_PYTHON3_ZLIB` (reading `/proc/config.gz`).
- Reason: smallest change that meets the plan; each removal saves build time without losing a feature.
- Alternatives considered: building host QEMU inside Buildroot (pins QEMU too, but adds ~15 minutes).
- Consequences: the QEMU version is the host's; record it in `build_info` if a QEMU problem ever appears.

## D-009: Kernel log buffer enlarged to 4 MB (`log_buf_len=4M`)
- Date / phase: 2026-11-15, Phase 1
- Context: each test finds its kernel-log lines by searching for a marker. Long tests (500 load/unload
  cycles) could push the marker out of the default 128 KB ring buffer.
- Decision: both QEMU command lines pass `log_buf_len=4M`. If a marker is still lost, the helper fails the
  test loudly instead of silently checking nothing.
- Reason: the log check must never pass by accident.
- Alternatives considered: changing `CONFIG_LOG_BUF_SHIFT` in the fragment (needs a kernel rebuild to tune).
- Consequences: uses 4 MB of the guest's 2048 MB.

## D-010: QEMU command line compared with Buildroot's reference
- Date / phase: 2026-11-15, Phase 1
- Context: the plan says to prefer Buildroot's generated start script as the reference and record differences.
- Decision: the reference (`board/qemu/aarch64-virt/readme.txt`) is used with these changes: `-smp 2`
  (instead of 1, so concurrency tests really run in parallel), `-m 2048` (KASAN needs memory),
  `hostfwd=tcp:127.0.0.1:5020-:502` (Modbus from the host), `log_buf_len=4M` (D-009). Autotest adds
  `-no-reboot -snapshot` and `panic=5 hvac_autotest=1`.
- Reason: `-snapshot` throws away the guest's disk writes, so every test run starts from the identical image
  and a crashed run cannot leave a half-written file system behind.
- Alternatives considered: copying the image before each run (slower, uses disk).
- Consequences: files created in the guest during `make test-qemu` do not persist (intended).

## D-011: Guest tests read the pinned kernel version from a copy of `VERSIONS.md`
- Date / phase: 2026-11-15, Phase 1
- Context: the smoke test must compare `uname -r` with the version recorded in `VERSIONS.md`.
- Decision: `post-build.sh` copies `VERSIONS.md` and `linux.fragment` into `/opt/hvac-tests/`; the test parses
  the `| Linux kernel | <version> |` row.
- Reason: one source of truth; no version number repeated in test code.
- Alternatives considered: hard-coding the version in the test.
- Consequences: keep the "Linux kernel" row format stable.

## D-012: `CONFIG_DEBUG_KERNEL=y` added to the kernel fragment
- Date / phase: 2026-11-15, Phase 1
- Context: the first smoke run showed `CONFIG_DEBUG_KMEMLEAK`, `CONFIG_PROVE_LOCKING` and
  `CONFIG_DEBUG_ATOMIC_SLEEP` missing from `/proc/config.gz`. In Linux 6.18 all three
  `depends on DEBUG_KERNEL` (`mm/Kconfig.debug`, `lib/Kconfig.debug`), which Buildroot's QEMU kernel
  config leaves unset, so the kernel configurator silently dropped them (pitfall in plan section 12).
- Decision: add `CONFIG_DEBUG_KERNEL=y` to `linux.fragment` (the smoke test now checks it too).
- Reason: it is the missing dependency; no other way to get kmemleak and lockdep.
- Alternatives considered: none.
- Consequences: the plan's fragment list (section 5.4) gains one line.

## D-013: I2C adapters are found under `/sys/bus/i2c/devices/i2c-N`
- Date / phase: 2026-11-15, Phase 1
- Context: the first smoke run could not find the i2c-stub adapter: Linux 6.18 no longer has the old
  `/sys/class/i2c-adapter` compatibility class.
- Decision: helpers and the demo init script scan `/sys/bus/i2c/devices/i2c-*/name` for
  `SMBus stub driver` (entries such as `1-0048` are client devices and are skipped).
- Reason: this is the current, documented location of adapters.
- Alternatives considered: `i2cdetect -l` output parsing (an extra tool and text format to depend on).
- Consequences: none.

## D-014: Boot-log exception for kmemleak's start-up line
- Date / phase: 2026-11-15, Phase 1
- Context: the clean-boot-log check matches the plain word `kmemleak` (ENGINEERING_RULES.md 7.5). With
  kmemleak enabled, every boot prints the informational line
  `kmemleak: Kernel memory leak detector initialized (mem pool available: N)`.
- Decision: the boot-log check allows exactly that line (anchored regular expression in
  `tests/guest/test_smoke.py`). No other boot line is excepted, and the per-test log checks have no
  exceptions at all (this line is printed only once, at boot).
- Reason: it reports that the checker started, not a problem. A real kmemleak report reads
  `kmemleak: N new suspected memory leaks` and would still fail the check.
- Alternatives considered: weakening the pattern to `kmemleak: .*leak` everywhere (would be broader).
- Consequences: none.

## D-015: Fictional device-tree vendor prefix `hvac`
- Date / phase: 2026-11-15, Phase 2
- Context: the plan fixes the compatible string `"hvac,hvac-temp"` and asks for it to be recorded.
- Decision: use it as given. `hvac` is not a registered vendor prefix in the kernel's
  `Documentation/devicetree/bindings/vendor-prefixes.yaml`; this is an out-of-tree learning driver.
- Reason: a real product would use its manufacturer's registered prefix (for example `ti,tmp102`).
- Alternatives considered: `ti,tmp102` (would collide with the real in-kernel tmp102 driver).
- Consequences: `checkpatch.pl` warns about the undocumented compatible (allowed in Phase 4, with reason).

## D-016: `in_temp_raw` / `in_temp_scale` are read-only by behavior, not by file mode
- Date / phase: 2026-11-15, Phase 3
- Context: the plan lists these files with mode `r`. The first Phase 3 run showed the IIO core in Linux
  6.18 creates every channel attribute as `0644` (it always installs its generic write handler).
- Decision: the test checks behavior instead: reading works and any write fails with `EINVAL` (the driver
  has no `write_raw()`). The two alarm files are checked for mode `0644` because the driver sets that.
- Reason: the file mode of channel attributes is decided by the IIO core, not by the driver; what matters
  to a user is that the value cannot be changed.
- Alternatives considered: none that keep using the standard IIO channel mechanism.
- Consequences: none for users.

## D-017: Test helpers use `i2cset -f` / `i2cget -f`
- Date / phase: 2026-11-15, Phase 3
- Context: once `hvac_temp` is bound at 0x48, the i2c-dev interface refuses that address with
  "Device or resource busy", so tests could no longer set the simulated chip's registers.
- Decision: `set_chip_reg()` / `get_chip_reg()` pass `-f` (force), which uses `I2C_SLAVE_FORCE`.
- Reason: on `i2c-stub` the "chip" is plain memory, so a forced access cannot disturb anything; the
  driver's mutex is not involved because the test talks to the stub directly, like a real chip changing
  its own temperature register.
- Alternatives considered: unbinding the driver around every register change (would not test live reads).
- Consequences: never copy this habit to real hardware without thinking.

## D-018: The I2C read-error path after probe is covered indirectly
- Date / phase: 2026-11-15, Phase 3
- Context: `i2c-stub` always answers at the addresses it simulates; it cannot be told to fail a transfer
  after the driver has probed. So `in_temp_raw` returning an I2C error cannot be triggered directly.
- Decision: the read-error path is covered by (1) the missing-chip test (probe's identical
  `i2c_smbus_read_word_swapped()` error path, Phase 2), (2) the remove-while-reading test (reads fail with
  an error code while the device goes away, Phase 4), and (3) code review: `read_raw` returns the negative
  error code unchanged, which IIO passes to `read()` as the error.
- Reason: honesty about what the simulated chip can and cannot do (plan Phase 3, task 6).
- Alternatives considered: a fault-injecting fake I2C adapter module (more kernel code to maintain and
  review than the path it would test).
- Consequences: listed under limitations in the technical report.

## D-019: No checkpatch warning types need to be allowed
- Date / phase: 2026-11-15, Phase 4
- Context: the plan expects some checkpatch warnings to be allowed (for example the undocumented
  device-tree compatible) and listed here.
- Decision: `ALLOWED_TYPES` in `scripts/static_analysis.sh` is empty. checkpatch 6.18 reports 0 errors and
  0 warnings for every `driver/` file. (With `--no-tree`, checkpatch cannot read the kernel's binding
  documentation, so the undocumented-compatible check does not run.)
- Reason: nothing to allow; the script still supports the list if a future change needs it.
- Alternatives considered: none.
- Consequences: if the compatible-string check is ever run with a full tree, `hvac,hvac-temp` would be
  reported (D-015) and would be added here with that reason.

## D-020: Mutation testing details
- Date / phase: 2026-11-15, Phase 4
- Context: the plan defines three mutants and asks why a "remove the mutex" mutant is not included.
- Decision: (1) the three patches exactly as planned; (2) a mutant that fails to compile is reported as
  `build-error`, not as killed (a compiler error proves nothing about the tests); (3) the script backs up
  `driver/` and restores it in a `trap`, then rebuilds the original image.
- Reason for no mutex mutant: removing the lock creates a race, and a race shows up only when two threads
  hit the same few instructions at the same moment. On `i2c-stub` each transfer is a quick memory access,
  so a test could pass thousands of times with the bug present. A mutant that is not reliably killed
  would make the score depend on luck. Lock correctness is instead checked by lockdep (`PROVE_LOCKING`)
  and by code review.
- Alternatives considered: adding delays inside the driver to widen the race (changes the code under test).
- Consequences: mutation score target 3/3.

## D-021: Control core: small additions and one known limitation
- Date / phase: 2026-11-15, Phase 5
- Context: section 4.4 fixes the core's interface and rules but not every helper or edge case.
- Decision:
  1. Added `hvac_config_set_defaults()` (fills the section 4.5 defaults) so the daemon, simulator, tests
     and FreeRTOS port share one source of defaults. No existing interface changed.
  2. `HVAC_TEMP_MIN_MC` / `HVAC_TEMP_MAX_MC` are defined again in `hvac_core.h` with the same values as
     `driver/hvac_temp_conv.h`: kernel and user-space headers cannot include each other (`u16`, kernel macros).
  3. NULL arguments: functions returning `int` give `-EINVAL`; `hvac_core_step()` returns `HVAC_STATE_FAULT`
     without touching anything.
  4. If the monotonic clock ever appears to go backwards, elapsed time counts as 0 (timers wait longer,
     never shorter).
- Known limitation (follows the plan exactly, documented rather than changed): the implausible-jump check
  compares with the last *valid* reading. If the sensor is missing while the room changes by more than
  `max_step_mc` (5 C), every new reading counts as a jump and the controller stays in FAULT until a
  restart. That fails safe (alarm on, equipment off). Revisit if the plan changes.
- Consequences: none for the public interface in section 4.4.

## D-022: Configuration parser details
- Date / phase: 2026-11-15, Phase 5
- Context: section 4.5 does not cover duplicate keys, line length, or which line a cross-field error names.
- Decision: a duplicate key is an error (`duplicate key 'x'`), like an unknown key, because it is almost
  always a mistake. Lines are at most 255 characters and files at most 8192 bytes. A file with a NUL byte
  is rejected. The band-ordering error is reported at the last line that set `cool_setpoint_mc`,
  `heat_setpoint_mc` or `deadband_mc` (line 0 if none was set). File-level errors (cannot open, too
  large) use line 0.
- Reason: simplest rules that give a useful line number for every error.
- Alternatives considered: "last value wins" for duplicates (hides typos).
- Consequences: none.

## D-023: Simulator definitions
- Date / phase: 2026-11-15, Phase 5
- Context: Phase 5 task 5 leaves some details of the room model and metrics open.
- Decision:
  1. Units: the simulator uses micro-degrees Celsius in `int64_t`, with suffix `_uc` (only inside `sim/`).
  2. Solar half-sine without floating point: Bhaskara I's approximation
     `sin(pi x) ~ 16x(1-x) / (5 - 4x(1-x))` (error below 0.2 %).
  3. Sensor: noise uniform in [-100000, +100000] µ°C from xorshift32 (seed 1 unless given). The noisy
     value is floored to a 62500 µ°C step (the chip's 0.0625 °C), then converted to millidegrees exactly
     like the driver chain (`raw x 62.5`, rounded half away from zero).
  4. Each second: read sensor, run controller, record metrics, then advance the room with the outputs.
  5. Baseline: compressor on when the reading is above 26000, off when below; at exactly 26000 it keeps
     its state (heater mirrored at 20000).
  6. `comfort_pct`: share of seconds from the first in-band second to the end of the day in which the
     true temperature is within 18.5..27.5 °C; 0 if the band is never entered.
     `time_to_comfort_min`: minutes until that first in-band second (null if never).
     `max_overshoot_mc`: largest distance outside 18.5..27.5 °C after first entering the band.
     `min_off_gap_s`: smallest off time between two compressor runs (null with fewer than two starts).
     `compressor_starts_per_hour`: starts / 24 h.
  7. Percentages are kept as integer hundredths and printed with two decimals (no floating point in C).
- Reason: simplest reading of the plan; every choice is deterministic so results are identical everywhere.
- Consequences: the CSV is sampled every 10 s; the plot's shading therefore has 10 s resolution.

## D-024: Daemon structure and behavior details
- Date / phase: 2026-11-15, Phase 6
- Context: sections 4.6 and 4.7 fix the daemon's behavior but not every detail.
- Decision:
  1. Extra file `src/modbus_regs.c` (+ header): the pure status <-> register mapping, separate from the
     libmodbus network code, so it is unit-tested without sockets. CMake builds `hvacd_support`
     (sensor, register map, log) without libmodbus; only `hvacd` links libmodbus.
  2. A Modbus setpoint write is detected by comparing registers 2 and 3 with the values the daemon last
     published. Both are then validated together with `hvac_core_set_setpoints()`.
  3. Each readable client gets one request answered per loop pass. A started request must complete
     within 200 ms (libmodbus indication/byte timeout), so a slow client cannot stall the control loop.
     A client beyond `modbus_max_clients` is accepted and closed immediately.
  4. Logging: state changes and output changes at INFO; the first sensor failure after a good read at WARN,
     and the recovery at INFO (not once per tick, so a missing sensor does not flood the log).
  5. If the loop falls behind (a tick is late), the next tick is scheduled one period after now: ticks
     are skipped, never run in a burst.
  6. The default `/etc/hvacd.conf` is installed by the `hvacd` Buildroot package from
     `thermostat/config/hvacd.conf` (single source, no copy in the overlay).
  7. Tests use port 15020 on the host and in the guest (the guest's port 502 belongs to the demo service).
  8. In the guest test, the fault-latency assertion uses the metric target from section 9
     (<= 3 x loop period + 1 s). The host test asserts the stricter 3 loop periods. The emulated guest adds
     Modbus polling overhead, which is not part of the daemon's reaction time.
- Reason: simplest designs that satisfy the plan and keep the control loop deterministic.
- Consequences: none for the public interfaces.

## D-025: Setpoint tests check the effect, not the read-back
- Date / phase: 2026-11-15, Phase 6
- Context: the first guest run failed `test_setpoint_writes_are_validated`. libmodbus stores a write in the
  register table immediately, so reading the register back succeeds *before* hvacd has validated it. The
  test wrote a valid and then an invalid setpoint within one loop period; hvacd (correctly) saw a single
  invalid change and restored the defaults. The earlier host test had the same blind spot: it would have
  passed even if hvacd ignored writes completely.
- Decision: both tests now prove a valid write was applied through its effect (a cooling setpoint below the
  room temperature must start cooling), then check that an invalid write restores the applied values.
- Reason: a test must fail when the behavior is missing.
- Consequences: none for the daemon (its behavior was correct and matches section 4.6).

## D-026: Metrics and CI details
- Date / phase: 2026-11-15, Phase 7
- Context: sections 4.9, 7 (Phase 7) and 9 leave a few details open.
- Decision:
  1. `sil_starts_per_hour_<scenario>_product` has target "< baseline" when the baseline started the
     compressor at all. On the cold day neither controller cools (0 and 0), so the target becomes "== 0".
     "Fewer than zero" cannot apply when there is no cooling demand.
  2. `build_duration_min` is the duration of the last *full* build, one that starts from an empty
     Buildroot output folder. The download cache and ccache may be warm, which the report states.
     `build.sh` records it as `full_build_duration_min`; incremental builds keep the old value.
  3. CI uses Python 3.14 (`actions/setup-python`), the same as the development host, so the fully pinned
     `requirements-host.txt` installs identically. The C toolchain and analyzers come from Ubuntu 24.04.
  4. The CI cache key covers `VERSIONS.md`, the defconfig and the kernel fragment. The Buildroot tarball
     is cached too.
  5. Guest metrics may carry an optional target (used by extensions); `collect_metrics.py` picks up any
     metric with a target that it does not already know.
- Reason: simplest choices that keep every target meaningful and CI reproducible.
- Consequences: none.

## D-027: Watchdog details (Ext 1)
- Date / phase: 2026-11-15, Ext 1
- Context: the plan names `wdt_open(path, timeout_s)`, `wdt_kick()`, `wdt_close_clean()` and says to kick
  "only after a successful control step".
- Decision:
  1. The three functions take a `wdt_t *` handle (plus `wdt_init()`), instead of hiding a global file
     descriptor. This keeps them testable and follows the "no hidden global state" spirit of the rules.
  2. "Successful control step" means the tick ran to completion (setpoints, sensor read, core step,
     publish). A *sensor* fault is a successful step: the software is alive and is correctly reporting
     FAULT. The watchdog is about the software hanging, not about the sensor.
  3. The kick uses the `WDIOC_KEEPALIVE` ioctl; the timeout is set with `WDIOC_SETTIMEOUT`; clean
     shutdown writes `V` before closing (magic close). If the timeout ioctl fails at start-up, the device
     is closed with magic close before returning the error, so no armed watchdog is left behind.
  4. Tests use `watchdog_timeout_s = 2` so the three guest tests take seconds, not minutes.
  5. The expiry text `softdog: Triggered - Reboot ignored` was read from
     `drivers/watchdog/softdog.c` of Linux 6.18.7 (`pr_crit("Triggered - Reboot ignored\n")` with the
     `softdog: ` prefix).
  6. The board's `/etc/hvacd.conf` enables the watchdog (the compiled-in default stays 0), and the demo
     init script loads `softdog` (default options, so a hang would really reboot the demo board).
- Consequences: `metrics.json` gains `watchdog_trigger_latency_s` (target `<= 5`).
  (Addendum after the first run: the guest test's cleanup disarms the watchdog with its own magic close
  before unloading `softdog`, because a daemon ended with SIGKILL leaves the watchdog running and the
  kernel then refuses to unload the module. That is the intended safety behavior, not a bug.)

## D-028: Fault detection details (Ext 2)
- Date / phase: 2026-11-15, Ext 2
- Context: the plan defines the two rules, their defaults and the experiment matrices, but not every
  detail.
- Decision:
  1. `hvac_ctx_t` gains `last_reading_valid` (set by every `hvac_core_step()`), so FDD uses exactly the
     core's notion of a valid reading. The field is additive; no existing interface changed.
  2. Config ranges: `fdd_enabled` 0..1 (default 1), `fdd_ineffective_window_s` and `fdd_stuck_window_s`
     60..86400 s, `fdd_ineffective_min_drop_mc` 100..10000.
  3. Cooling-ineffective window: it starts at the first valid reading with the compressor on, and is
     re-referenced at the end of each window. Invalid readings are skipped (neither start nor judge a
     window). "Drop" is reference minus current; a rise counts as a negative drop.
  4. Sensor-stuck: any change of the valid reading (even one 1/16 °C step) restarts the timer.
  5. Simulator faults (from 06:00): `capacity_loss` = cooling at 40 % strength, `compressor_fail` =
     cooling at 0 %, `sensor_stuck` = the simulated sensor repeats its last reading. The noise generator
     still advances while stuck, so a run's random sequence does not depend on the fault.
  6. Detection-matrix runs use seed 1. Time to detect is measured from the injection to the first step
     where the expected bit is set. False-alarm runs count a day as a false alarm if *any* bit is set by
     the end of the day.
  7. The detection target (100 %) and false-alarm target (0) are checked in `make test-host`
     (`scripts/check_fdd.py`).
- Reason: simplest reading of the plan; all deterministic.
- Consequences: the rules only see faults in equipment that is being used (see the technical report).

## D-029: MQTT telemetry details (Ext 3)
- Date / phase: 2026-11-16, Ext 3
- Context: the plan fixes the library, topics, payload fields, config keys and tests, but not the
  file split, the exact JSON, the connection mechanics or how to measure the loop period.
- Decision:
  1. Files: the pure formatter `telemetry_format_json()` lives in `src/telemetry_json.c`
     (`include/telemetry_json.h`, no libmosquitto), the network client in `src/telemetry_mqtt.c`
     (`include/telemetry_mqtt.h`). One responsibility per file, and the formatter's unit tests (and a
     later FreeRTOS port) need no MQTT library. Additive; no existing interface changes.
  2. Payload (one line, keys in the plan's order), for example
     `{"device":"hvac-01","seq":7,"uptime_s":35,"temp_c":27.06,"state":"COOLING","compressor":true,`
     `"heater":false,"fan":true,"alarms":0,"fdd":0,"compressor_starts":1}`.
     `temp_c` is rounded exactly like Modbus register 0 (`mb_mc_to_cc()`), and is `null` before the first
     valid reading. `state` is `hvac_state_name()`. `alarms` and `fdd` are the bit values of registers 5
     and 8. The formatter returns the length, `-ENOSPC` on truncation (nothing is published) or `-EINVAL`.
  3. Connection: MQTT 3.1.1, client id = `mqtt_device_id`, clean session, keep-alive 30 s.
     `mosquitto_connect_async()` / `mosquitto_reconnect_async()` (non-blocking TCP connect) and
     `mosquitto_loop(mosq, 0, 1)` once per loop iteration; the MQTT socket is also added to the
     `select()` read set so incoming acknowledgements are handled promptly. Reconnect attempts are at
     least 5 s apart. Limitation: a host *name* is resolved with a blocking DNS lookup, so the board
     configuration uses an IP address.
  4. Publishing: telemetry (QoS 1, not retained) only while connected. `seq` increases every period
     whether or not the message could be sent, so a subscriber sees a gap for every missed message.
     Status topic: retained `online` (QoS 1) after each connect; last will `offline` (retained, QoS 1);
     on a clean shutdown hvacd publishes retained `offline` itself and waits at most 1 s for it to be sent.
  5. Memory: hvacd's own code still allocates nothing in the loop. libmosquitto allocates internally
     per packet and frees it when sent or acknowledged; because hvacd publishes only while connected,
     at most a few messages are ever queued. The Valgrind daemon test runs with MQTT enabled.
  6. Loop-period measurement: at `log_level = debug`, hvacd logs one `tick <ms>` line per control tick
     (milliseconds since start, monotonic clock). The guest test computes each tick interval while the
     broker is down; `control_loop_jitter_broker_down_ms` = largest |interval - loop_period_ms|.
  7. Tests (host and guest) start their own broker on 127.0.0.1:18830 with a generated configuration,
     no persistence. The board's default broker (Buildroot `S50mosquitto`, port 1883, localhost only)
     serves the demo; the board configuration enables MQTT to 127.0.0.1:1883 (compiled-in default 0).
  8. Metrics: `mqtt_messages_received_ratio` = messages received / (last seq - first seq + 1) over a
     healthy 10-period window (target 1.0); `mqtt_reconnect_s` = broker restart to `online` seen again
     (target <= 15); `control_loop_jitter_broker_down_ms` (target <= 100).
- Reason: simplest reading of the plan that keeps control independent of the network.
- Consequences: `hvacd` links libmosquitto; the Buildroot image gains mosquitto (broker, library, clients).
  (Addendum after the first CI run, 2026-11-16: the host job hung in pytest until the 60-minute job
  timeout, with `mosquitto` and `mosquitto_sub` still alive. `mosquitto_sub`'s SIGTERM handler
  (client/sub_client.c, the same in 2.0.18 on CI and 2.0.22 locally) calls `mosquitto_disconnect_v5()`
  inside the signal handler. That function takes library mutexes, which is not async-signal-safe, so
  a badly timed SIGTERM can deadlock the subscriber, and the test helper waited for it without a
  limit. Fix: the helper ends `mosquitto_sub` with SIGKILL (its output is already flushed) and waits
  at most 10 s. hvacd is not affected; it only sets a flag in its signal handler.)
