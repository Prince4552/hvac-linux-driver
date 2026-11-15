# SPDX-License-Identifier: MIT
"""Totals the host test results in results/host/ into host_summary.json.

Counts: unit tests (normal build), host pytest suites (if run), and SIL checks. The sanitizer and
Valgrind runs repeat the same unit tests, so they are reported separately, not added again.
Usage: host_summary.py RESULTS_HOST_DIR
"""
import json
import os
import sys


def load(directory: str, name: str) -> dict:
    path = os.path.join(directory, name)
    if not os.path.isfile(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main(directory: str) -> int:
    unit = load(directory, "unit_tests.json")
    pytest_host = load(directory, "pytest_host.json")
    sil = load(directory, "sil_check.json")
    passed = unit.get("passed", 0) + pytest_host.get("passed", 0)
    failed = unit.get("failed", 0) + pytest_host.get("failed", 0) + sil.get("checks_failed", 0)
    summary = {"schema": 1, "host_tests_passed": passed, "host_tests_failed": failed,
               "unit_tests_passed": unit.get("passed", 0),
               "pytest_host_passed": pytest_host.get("passed", 0),
               "sil_checks_failed": sil.get("checks_failed", 0)}
    with open(os.path.join(directory, "host_summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    print(f"[test-host] summary: {passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
