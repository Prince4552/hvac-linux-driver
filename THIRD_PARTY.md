# Third-party components

Every external component this project uses, its pinned version and its license.
Only Unity is copied into the repository (vendored); everything else is downloaded at build time
into `.cache/` or installed on the host. Exact versions and checksums are in `VERSIONS.md`.

| Component | Used for | Version | License | In repo? |
| --- | --- | --- | --- | --- |
| pytest | Host and guest test runner | see `requirements-host.txt` | MIT | no (pip) |
| pyflakes | Python static check | see `requirements-host.txt` | MIT | no (pip) |
| gcovr | C coverage reports | see `requirements-host.txt` | BSD-3-Clause | no (pip) |
| matplotlib | Simulation plots | see `requirements-host.txt` | PSF-based (matplotlib license) | no (pip) |
| Buildroot | Builds the ARM64 image | 2026.02.3 | GPL-2.0-or-later | no (`.cache/`) |
| Linux kernel | Guest kernel, `i2c-stub`, IIO, KUnit | 6.18.7 | GPL-2.0 | no (`.cache/`) |
| Bootlin toolchain | Cross-compiler (GCC, glibc) | aarch64 glibc stable 2025.08-1 | GPL / LGPL (per component) | no (`.cache/`) |
| BusyBox | Guest shell and basic tools | 1.37.0 (Buildroot 2026.02.3) | GPL-2.0 | no (`.cache/`) |
| i2c-tools | `i2cset`, `i2cget`, `i2cdetect` in the guest | 4.4 | GPL-2.0-or-later | no (`.cache/`) |
| Python 3 / pytest (guest) | Guest test runner | 3.14 / 9.0.2 | PSF-2.0 / MIT | no (`.cache/`) |
| libmodbus | Modbus TCP server in `hvacd` (guest 3.1.11 via Buildroot; host package for host tests) | 3.1.11 | LGPL-2.1-or-later (dynamically linked) | no |
| Unity | C unit-test framework | v2.7.0 | MIT | **yes**, `thermostat/third_party/unity/` (unchanged copy + license) |
| cppcheck, Valgrind, GCC sanitizers, gcov | Host checks | host packages | GPL (tools only, nothing linked into our code) | no (host) |
| QEMU | Emulates the ARM64 board | host package `qemu-system-arm` (10.2.1 on the development host) | GPL-2.0 | no (host) |

Extensions add rows (mosquitto, FreeRTOS-Kernel) when they bring them in.
