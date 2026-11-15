# SPDX-License-Identifier: MIT
"""Phase 3: the chip register value set through i2c-stub comes out of in_temp_raw correctly.

Covers the reference table (PROJECT_PLAN.md section 4.3), registers whose two bytes differ (so a
missing byte swap would be caught), and the ignored low 4 bits.
"""
import pytest

import hvac_test_lib as h

# (description, chip register, expected raw, expected millidegrees)
REFERENCE = [
    ("+125 C", 0x7D00, 2000, 125000),
    ("+25 C", 0x1900, 400, 25000),
    ("+0.0625 C", 0x0010, 1, 63),
    ("0 C", 0x0000, 0, 0),
    ("-0.0625 C", 0xFFF0, -1, -63),
    ("-25 C", 0xE700, -400, -25000),
    ("-40 C", 0xD800, -640, -40000),
]
# Both bytes non-zero and different: a driver that forgets the byte swap reads garbage.
BYTE_ORDER = [
    ("0x1230", 0x1230, 0x123, 18188),
    ("0x0120", 0x0120, 0x012, 1125),
    ("0xF350", 0xF350, -203, -12688),
    ("low bits ignored", 0x190F, 400, 25000),
]


def raw_times_scale_mc(raw: int, scale_text: str) -> int:
    """raw x scale in millidegrees, rounded half away from zero (integer math only)."""
    whole, frac = scale_text.split(".")
    scale_micro = int(whole) * 1_000_000 + int(frac.ljust(6, "0")[:6])
    product = raw * scale_micro
    half = 500_000
    return (product + half) // 1_000_000 if product >= 0 else -((-product + half) // 1_000_000)


@pytest.mark.parametrize("label,reg,raw,mc", REFERENCE + BYTE_ORDER,
                         ids=[r[0] for r in REFERENCE + BYTE_ORDER])
def test_register_value_reads_back_as_raw_and_millidegrees(hvac_device: int, label: str,
                                                           reg: int, raw: int, mc: int) -> None:
    h.set_chip_reg(0x00, reg)
    ok, value = h.read_attr("in_temp_raw")
    assert ok, f"read failed with errno {value}"
    assert int(value) == raw
    ok, scale = h.read_attr("in_temp_scale")
    assert ok
    assert raw_times_scale_mc(int(value), str(scale)) == mc


def test_set_temp_raw_helper_matches_driver(hvac_device: int) -> None:
    for raw in (-2048, -641, -1, 0, 1, 399, 2047):
        h.set_temp_raw(raw)
        assert h.read_attr("in_temp_raw") == (True, str(raw))
