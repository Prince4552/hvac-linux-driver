# hvac-linux-driver

A Linux kernel driver for an I2C (Inter-Integrated Circuit) temperature sensor, a test suite that
proves it correct and memory safe without any real hardware, and a thermostat daemon for an HVAC
(heating, ventilation and air conditioning) unit. Everything is built with Buildroot and tested on an
emulated ARM64 board in QEMU.

**Status: work in progress.** The project is built phase by phase; see
[docs/PROGRESS.md](docs/PROGRESS.md) for what is done and [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md)
for the full plan.

Quick check of your machine:

```
make check-env
```
