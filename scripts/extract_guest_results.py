# SPDX-License-Identifier: MIT
"""Extracts the guest test results from the QEMU console log (PROJECT_PLAN.md section 4.8).

Writes <out_dir>/summary.json, <out_dir>/guest_metrics.json and the guest kernel log, prints a short
report, and exits 1 if a marker is missing, pytest's exit code is non-zero, no test ran, or any test
failed, errored or was skipped. Standard library only (also used in CI).

Usage: extract_guest_results.py CONSOLE_LOG OUT_DIR DMESG_LOG
"""
import json
import re
import sys

BEGIN, END = "===HVAC-RESULTS-BEGIN===", "===HVAC-RESULTS-END==="
DMESG_BEGIN, DMESG_END = "===HVAC-DMESG-BEGIN===", "===HVAC-DMESG-END==="
DONE_RE = re.compile(r"^===HVAC-AUTOTEST-DONE exit=(-?\d+)===$")


def between(lines: list[str], begin: str, end: str) -> list[str] | None:
    """Lines strictly between the begin and end markers, or None if either is missing."""
    try:
        i = lines.index(begin)
        j = lines.index(end, i + 1)
    except ValueError:
        return None
    return lines[i + 1:j]


def main(console_log: str, out_dir: str, dmesg_log: str) -> int:
    with open(console_log, encoding="utf-8", errors="replace") as f:
        lines = [line.rstrip("\r\n").rstrip("\r") for line in f]

    problems = []
    result_lines = between(lines, BEGIN, END)
    dmesg_lines = between(lines, DMESG_BEGIN, DMESG_END)
    exit_codes = [int(m.group(1)) for m in map(DONE_RE.match, lines) if m]

    if dmesg_lines is None:
        problems.append("guest kernel log markers missing")
    else:
        with open(dmesg_log, "w", encoding="utf-8") as f:
            f.write("\n".join(dmesg_lines) + "\n")

    if result_lines is None:
        problems.append("result markers missing (guest crashed, hung or timed out?)")
        summary = None
    else:
        summary = json.loads("".join(result_lines))
        with open(f"{out_dir}/summary.json", "w", encoding="utf-8") as f:
            json.dump({k: v for k, v in summary.items() if k != "metrics"}, f, indent=1)
        with open(f"{out_dir}/guest_metrics.json", "w", encoding="utf-8") as f:
            json.dump({"schema": 1, "metrics": summary.get("metrics", {})}, f, indent=1)

    if not exit_codes:
        problems.append("AUTOTEST-DONE marker missing")
    elif exit_codes[-1] != 0:
        problems.append(f"pytest exit code {exit_codes[-1]}")

    if summary is not None:
        print(f"[results] kernel {summary.get('kernel')}: passed {summary['passed']}, "
              f"failed {summary['failed']}, errors {summary['errors']}, "
              f"skipped {summary['skipped']}, {summary.get('duration_s')} s")
        for test in summary.get("tests", []):
            if test["outcome"] != "passed":
                print(f"[results]   {test['outcome'].upper()}: {test['name']}")
        bad = summary["failed"] + summary["errors"] + summary["skipped"]
        if bad > 0:
            problems.append(f"{bad} test(s) failed, errored or skipped")
        if summary["passed"] == 0:
            problems.append("no test passed (did any run?)")

    for p in problems:
        print(f"[results] FAIL: {p}")
    if problems:
        print(f"[results] see {console_log}")
        return 1
    print("[results] guest tests: PASS")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3]))
