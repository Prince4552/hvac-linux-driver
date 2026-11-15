# SPDX-License-Identifier: MIT
"""Phase 3: alarm-limit attributes (PROJECT_PLAN.md section 4.2).

Writes are stored in the chip rounded to 0.0625 C steps and read back rounded to millidegrees.
Bad input returns EINVAL and leaves the chip register unchanged.
"""
import errno

import pytest

import hvac_test_lib as h

ATTRS = {"alarm_low_millicelsius": 0x02, "alarm_high_millicelsius": 0x03}
# (written value, expected chip register, expected read-back)
VALUES = [
    ("25000", 0x1900, "25000"),
    ("-10000", 0xF600, "-10000"),
    ("0", 0x0000, "0"),
    ("125000", 0x7D00, "125000"),
    ("-40000", 0xD800, "-40000"),
    ("62", 0x0010, "63"),    # 62 m°C rounds to 1 step = 62.5 m°C, read back as 63
    ("31", 0x0000, "0"),     # 31 m°C rounds to 0 steps
]
# "\n" is what `echo > file` writes: an empty value.
INVALID = ["abc", "\n", "125001", "-40001", "99999999999"]


@pytest.mark.parametrize("attr", sorted(ATTRS))
@pytest.mark.parametrize("written,reg,read_back", VALUES, ids=[v[0] for v in VALUES])
def test_alarm_write_stores_rounded_register_and_reads_back(hvac_device: int, attr: str,
                                                            written: str, reg: int,
                                                            read_back: str) -> None:
    assert h.write_attr(attr, written) == (True, None)
    assert h.get_chip_reg(ATTRS[attr]) == reg
    assert h.read_attr(attr) == (True, read_back)


@pytest.mark.parametrize("attr", sorted(ATTRS))
@pytest.mark.parametrize("bad", INVALID, ids=["abc", "empty", "125001", "-40001", "huge"])
def test_invalid_alarm_write_is_rejected_and_register_unchanged(hvac_device: int, attr: str,
                                                                bad: str) -> None:
    h.set_chip_reg(ATTRS[attr], 0x1230)
    assert h.write_attr(attr, bad) == (False, errno.EINVAL)
    assert h.get_chip_reg(ATTRS[attr]) == 0x1230


def test_low_above_high_is_allowed(hvac_device: int) -> None:
    """The chip does not require low < high, so neither does the driver (documented)."""
    assert h.write_attr("alarm_high_millicelsius", "10000") == (True, None)
    assert h.write_attr("alarm_low_millicelsius", "30000") == (True, None)
