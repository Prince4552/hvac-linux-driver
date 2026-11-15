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
| BusyBox, i2c-tools, Python 3, pytest, libmodbus | Guest user space | as pinned by Buildroot 2026.02.3 | GPL-2.0 / GPL-2.0 / PSF / MIT / LGPL-2.1 | no (`.cache/`) |
| QEMU | Emulates the ARM64 board | host package (`qemu-system-arm`) | GPL-2.0 | no (host) |

Rows are added as phases bring in new components (Buildroot, Linux, toolchain, Unity, libmodbus,
mosquitto, FreeRTOS-Kernel).
