# SPDX-License-Identifier: MIT
"""pytest fixtures shared by all guest tests (PROJECT_PLAN.md section 6.2).

- check_kernel_log (autouse): after every test, the kernel log written during the test must be
  free of BUG/WARNING/Oops/KASAN/... lines. Runs as teardown, so it also runs when a test fails.
- stub: the simulated chip (i2c-stub at 0x48) is loaded for the test and unloaded afterwards.
- hvac_device: stub + hvac_temp module + device at 0x48 with the driver bound; torn down in
  reverse order (delete device, unload module, unload stub) even when the test fails.
"""
import os
from typing import Iterator

import pytest

import hvac_test_lib as h


@pytest.fixture(autouse=True)
def check_kernel_log() -> Iterator[None]:
    marker = h.kernel_log_marker()
    yield
    h.assert_kernel_log_clean(h.kernel_log_since(marker))


@pytest.fixture
def stub() -> Iterator[int]:
    """Yields the bus number of a freshly loaded i2c-stub; always unloads it afterwards."""
    h.unload_stub()  # start from a known state even if an earlier test failed badly
    bus = h.load_stub()
    try:
        yield bus
    finally:
        h.unload_stub()


BIND_TIMEOUT_S = 10.0


@pytest.fixture
def hvac_device(stub: int) -> Iterator[int]:
    """Yields the stub bus number with hvac_temp bound to the simulated chip at 0x48."""
    h.run(["modprobe", "hvac_temp"])
    try:
        h.new_device(h.CHIP_ADDR)
        try:
            assert h.wait_until(lambda: h.bound_driver() == h.DRIVER_NAME, BIND_TIMEOUT_S)
            yield stub
        finally:
            if os.path.exists(h.client_dir()):
                h.delete_device(h.CHIP_ADDR)
    finally:
        h.run(["modprobe", "-r", "hvac_temp"])
