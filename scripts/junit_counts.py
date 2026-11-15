# SPDX-License-Identifier: MIT
"""Counts passed/failed/error/skipped test cases in a pytest JUnit XML report.

Writes {"passed", "failed", "errors", "skipped"} to OUT_JSON and exits 1 if anything but
"passed" occurred (skipped tests also fail a gate).
Usage: junit_counts.py JUNIT_XML OUT_JSON
"""
import json
import sys
import xml.etree.ElementTree as ET


def main(xml_path: str, out_path: str) -> int:
    counts = {"passed": 0, "failed": 0, "errors": 0, "skipped": 0}
    for case in ET.parse(xml_path).getroot().iter("testcase"):
        if case.find("failure") is not None:
            counts["failed"] += 1
        elif case.find("error") is not None:
            counts["errors"] += 1
        elif case.find("skipped") is not None:
            counts["skipped"] += 1
        else:
            counts["passed"] += 1
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"schema": 1, **counts}, f)
    print(f"[pytest] {counts}")
    return 0 if counts["failed"] + counts["errors"] + counts["skipped"] == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
