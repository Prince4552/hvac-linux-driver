# Pinned versions

Every external download uses an exact version (or commit) recorded here, so anyone can reproduce
the exact same build. "Why pin?" A floating version ("latest") can change under you and break a
build that worked yesterday; a pinned one cannot.

| Component | Version | Checksum / commit | Where it is fetched |
| --- | --- | --- | --- |
| Host Python tools | see `requirements-host.txt` | pip resolves exact `==` pins | PyPI |
| Buildroot | 2026.02.3 | SHA-256 `5a59e7501b0b4ec52c41f4bfa79412320e0b37eae5f719605a258e8d0c6fc7fb` (matches official `.sign`) | https://buildroot.org/downloads/ |
| Linux kernel | 6.18.7 | hash checked by Buildroot (`linux.hash`) | pinned by Buildroot 2026.02.3 `qemu_aarch64_virt_defconfig` |
| Toolchain | Bootlin aarch64 glibc stable 2025.08-1 | hash checked by Buildroot | toolchains.bootlin.com (via `BR2_TOOLCHAIN_EXTERNAL_BOOTLIN`) |
| Unity (vendored) | v2.7.0 | commit `b6763fbd9cedfacaa89e2ad9fd00d615a234e355`; archive SHA-256 `e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c` | github.com/ThrowTheSwitch/Unity (copied into `thermostat/third_party/unity/`) |
| Target packages | versions fixed by Buildroot 2026.02.3 (python3, pytest, i2c-tools, libmodbus, ...) | hashes checked by Buildroot (`BR2_DOWNLOAD_FORCE_CHECK_HASHES=y`) | Buildroot package mirrors |

Rows are added as phases bring in new components.
