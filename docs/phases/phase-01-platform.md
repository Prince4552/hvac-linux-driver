# Phase 1: Platform (Buildroot + QEMU + automated guest testing)

## 1. What this phase adds

One command (`make build`) builds a complete ARM64 Linux system: kernel plus root filesystem. One
command (`make test-qemu`) boots it in an emulated board, runs the tests inside it, and brings the
results back to the host. Every later phase builds on this platform.

## 2. Why it matters

HVAC controllers run embedded Linux on ARM chips. Teams build their images with tools like Buildroot
or Yocto, and test them on emulators before hardware exists. A test that runs automatically on every
change is worth far more than a manual check on a lab board.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `scripts/common.sh` | Shared settings: pinned Buildroot version, folders, clean `PATH` |
| `scripts/fetch_buildroot.sh` | Downloads Buildroot 2026.02.3 and checks its SHA-256 |
| `scripts/build.sh` | Runs the Buildroot build, writes `results/host/build_info.json` |
| `scripts/qemu_common.sh` | The QEMU command line, shared by the two scripts below |
| `scripts/run_qemu.sh` | Boots the board interactively (`make run`) |
| `scripts/test_qemu.sh` | Boots in autotest mode and checks the results (`make test-qemu`) |
| `scripts/extract_guest_results.py` | Cuts the result block out of the console log |
| `scripts/test_host.sh` | Host tests (lint for now) |
| `br2-external/` | Our Buildroot customization: defconfig, kernel fragment, overlay, post-build hook |
| `tests/guest/run_guest_tests.sh` | Runs pytest inside the board and prints the result markers |
| `tests/guest/hvac_test_lib.py`, `conftest.py` | Shared test helpers and fixtures |
| `tests/guest/make_summary.py` | Turns pytest's JUnit XML into the one-line JSON summary |
| `tests/guest/test_smoke.py` | Checks that the platform has everything later tests need |

## 4. How it works

```
 make build                               make test-qemu
 ----------                               --------------
 fetch Buildroot (pinned, checksummed)    qemu-system-aarch64 ... hvac_autotest=1
 apply hvac_qemu_aarch64_defconfig          | kernel boots, init runs /etc/init.d/S*
 download Bootlin cross-compiler            | S99autotest sees hvac_autotest=1
 build kernel 6.18.7 + fragment             | run_guest_tests.sh -> pytest
 build BusyBox, Python, pytest, i2c-tools   | prints ===HVAC-RESULTS-BEGIN=== {json} ...
 post-build.sh copies tests/guest in        | poweroff
 -> Image + rootfs.ext4                   console log -> extract_guest_results.py
                                          -> results/guest/summary.json, PASS/FAIL
```

1. **Cross-compilation.** The host is x86-64 and the board is ARM64. The Bootlin toolchain is a compiler
   that runs on x86 but produces ARM code.
2. **Buildroot** reads the defconfig (a list of choices), downloads every source at a pinned version,
   checks its hash, builds it, and packs the result into a kernel `Image` and an ext4 disk image.
3. **Kernel configuration.** `linux.fragment` adds options on top of Buildroot's QEMU kernel config:
   I2C, the `i2c-stub` simulated chip, IIO, KUnit, and the memory checkers KASAN, kmemleak and lockdep.
   An option with unmet dependencies is silently dropped, so a smoke test checks every line against
   `/proc/config.gz` on the running board.
4. **Root filesystem overlay.** Files in `rootfs-overlay/` are copied over the image as they are. The init
   scripts there decide between demo mode and autotest mode by reading `/proc/cmdline`.
5. **Getting results out.** The only channel from the virtual board is its serial console. The runner
   prints the results between fixed marker lines. It first runs `dmesg -n 1`, so kernel messages cannot
   land in the middle of the JSON. The host cuts the block out of the log.

**Yocto mapping (used in industry):** Buildroot defconfig ≈ Yocto `local.conf` plus image recipe;
`br2-external` ≈ a Yocto layer; a Buildroot package `.mk` ≈ a Yocto `.bb` recipe.

## 5. Run it yourself

```
make build        # first time 30-90 min; log: results/logs/build.log
make test-qemu    # boots, tests, powers off; expect "[results] guest tests: PASS"
make run          # interactive; log in as root; quit with Ctrl-A then X
```

Inside `make run`, try `zcat /proc/config.gz | grep I2C_STUB` and `i2cdetect -l`.

## 6. Tests and results

Gate `make test-all` on 2026-11-15: host lint clean; guest **34 passed, 0 failed, 0 skipped**.

| Test | What it checks | Result |
| --- | --- | --- |
| `test_machine_is_aarch64` | The board really is ARM64 | pass |
| `test_kernel_version_matches_versions_md` | Kernel is the pinned 6.18.7 | pass |
| `test_kernel_config_option_took_effect` (22 cases) | Every line of `linux.fragment` is in the running kernel | pass |
| `test_debugfs_is_mounted`, `test_debugfs_entry_exists` (2) | kmemleak and KUnit debug files exist | pass |
| `test_i2c_stub_round_trip_and_clean_unload` | Simulated chip stores and returns a register, then unloads | pass |
| `test_tool_is_present` (4), `test_pytest_is_present` | python3, pytest, i2cset, i2cget, i2cdetect | pass |
| `test_boot_log_is_clean` | No BUG/WARNING/Oops/KASAN/... at boot (one recorded exception, D-014) | pass |

| Number | Value |
| --- | --- |
| First full build | 13.8 min (host tools, toolchain, kernel, Python) |
| Kernel image / root filesystem used | 38 MB / 54 MB |
| Boot to start of tests (`guest_boot_to_tests_s`) | about 9 s (emulated, relative only) |
| Whole autotest run (boot, tests, power off) | about 20 s |

**What the smoke test caught on its very first run:** three kernel options (kmemleak, lockdep,
atomic-sleep checking) had been silently dropped because they need `CONFIG_DEBUG_KERNEL` (D-012), and
the i2c adapter folder had moved in Linux 6.18 (D-013). Without these checks, Phase 4's memory tests
would have "passed" while checking nothing.

## 7. Key concepts

- **Kernel vs root filesystem.** The kernel is one file (`Image`) that manages hardware. The root
  filesystem holds every program (shell, Python, our tests).
- **QEMU.** A program that pretends to be a whole computer. Here it emulates an ARM64 board in software,
  without KVM (Kernel-based Virtual Machine acceleration), so it is slow but exact.
- **`-snapshot`.** The test run never changes the disk image, so every run starts identical.
- **Markers on the console.** A simple, robust protocol. If any marker is missing, the run failed,
  even if no test reported a failure.

## 8. What could go wrong

- WSL puts Windows folders with spaces in `PATH`, and Buildroot refuses to build. `common.sh` sets a
  clean `PATH`.
- A kernel option can vanish quietly. The smoke test catches it.
- A kernel panic could hang the run. `panic=5` + `-no-reboot` + `timeout 60m` always end it.

## 9. Interview questions

1. *What is cross-compilation?* Compiling on one CPU type (x86) for another (ARM), using a toolchain
   built for that pair.
2. *Buildroot vs Yocto?* Both build embedded Linux images from source. Buildroot is simpler and
   builds one image. Yocto is layered and produces packages and SDKs, and scales to product families.
3. *How do you test a kernel without hardware?* Emulate the board (QEMU) and simulate the device
   (`i2c-stub`), then run the tests automatically inside the emulator.
4. *How do results leave the VM?* Printed between markers on the serial console and parsed on the host.
