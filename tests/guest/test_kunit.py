# SPDX-License-Identifier: MIT
"""Phase 3: the KUnit suite hvac_temp_conv runs inside the kernel and every case passes."""
import re

import hvac_test_lib as h

RESULTS = "/sys/kernel/debug/kunit/hvac_temp_conv/results"
EXPECTED_CASES = 5


def test_kunit_conversion_suite_passes() -> None:
    h.run(["modprobe", "hvac_temp_kunit"])
    try:
        with open(RESULTS, encoding="utf-8") as f:
            text = f.read()
    finally:
        h.run(["modprobe", "-r", "hvac_temp_kunit"])
    # KTAP output: each case is "ok N name" or "not ok N name" (indented inside the suite).
    passed = re.findall(r"^\s+ok \d+ (\w+)", text, re.MULTILINE)
    failed = re.findall(r"^\s+not ok \d+ (\w+)", text, re.MULTILINE)
    assert failed == [], text
    assert len(passed) == EXPECTED_CASES, text
    h.record_metric("kunit_cases_passed", len(passed), "count",
                    "Kernel-level checks of the conversion math (KUnit cases passed)")
