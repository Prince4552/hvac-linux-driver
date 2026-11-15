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

Rows are added as phases bring in new components (Buildroot, Linux, toolchain, Unity, libmodbus,
mosquitto, FreeRTOS-Kernel).
