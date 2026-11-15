# SPDX-License-Identifier: MIT
"""Phase 3: the IIO device has the fixed name and scale from PROJECT_PLAN.md section 4.2."""
import errno
import os

import pytest

import hvac_test_lib as h


def test_iio_device_name_is_hvac_temp(hvac_device: int) -> None:
    assert h.read_attr("name") == (True, "hvac_temp")


def test_scale_is_62_5_millidegrees_per_step(hvac_device: int) -> None:
    assert h.read_attr("in_temp_scale") == (True, "62.500000")


def test_alarm_files_are_read_write(hvac_device: int) -> None:
    d = h.iio_dir()
    assert d is not None
    for name in ("alarm_low_millicelsius", "alarm_high_millicelsius"):
        assert os.stat(os.path.join(d, name)).st_mode & 0o777 == 0o644, name


@pytest.mark.parametrize("name", ["in_temp_raw", "in_temp_scale"])
def test_channel_files_are_read_only_in_behavior(hvac_device: int, name: str) -> None:
    """The IIO core gives channel files mode 0644, but without write_raw() a write fails (D-016)."""
    ok, _ = h.read_attr(name)
    assert ok
    assert h.write_attr(name, "1") == (False, errno.EINVAL)
