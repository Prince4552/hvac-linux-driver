# SPDX-License-Identifier: MIT
"""Checks per-file line and branch coverage from a gcovr JSON summary against targets.

Usage: check_coverage.py GCOVR_SUMMARY_JSON OUT_JSON FILE:MIN_LINE_PCT:MIN_BRANCH_PCT ...
Writes OUT_JSON with the numbers for every source file and exits 1 if a target is missed.
"""
import json
import os
import sys


def main(summary_path: str, out_path: str, targets: list[str]) -> int:
    with open(summary_path, encoding="utf-8") as f:
        files = {os.path.basename(e["filename"]): e for e in json.load(f)["files"]}
    report = {name: {"line_pct": e["line_percent"], "branch_pct": e["branch_percent"]}
              for name, e in sorted(files.items())}
    failures = []
    for target in targets:
        name, min_line, min_branch = target.split(":")
        entry = report.get(name)
        if entry is None:
            failures.append(f"{name}: no coverage data")
            continue
        if entry["line_pct"] < float(min_line):
            failures.append(f"{name}: lines {entry['line_pct']}% < {min_line}%")
        if entry["branch_pct"] < float(min_branch):
            failures.append(f"{name}: branches {entry['branch_pct']}% < {min_branch}%")
    core = report.get("hvac_core.c", {"line_pct": 0.0, "branch_pct": 0.0})
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"schema": 1, "core_line_coverage_pct": core["line_pct"],
                   "core_branch_coverage_pct": core["branch_pct"], "files": report,
                   "failures": failures}, f, indent=1)
    for name, entry in report.items():
        print(f"[coverage] {name}: lines {entry['line_pct']}%, branches {entry['branch_pct']}%")
    for line in failures:
        print(f"[coverage] FAIL: {line}")
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3:]))
