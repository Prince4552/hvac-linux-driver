# SPDX-License-Identifier: MIT
"""Builds the one-line JSON result summary printed by the guest runner.

Reads pytest's JUnit XML report and the per-metric JSON files written by record_metric(), and
prints the summary described in PROJECT_PLAN.md section 4.8. Standard library only.
"""
import json
import os
import sys
import xml.etree.ElementTree as ET


def _outcome(case: ET.Element) -> str:
    if case.find("failure") is not None:
        return "failed"
    if case.find("error") is not None:
        return "error"
    if case.find("skipped") is not None:
        return "skipped"
    return "passed"


def read_junit(path: str) -> tuple[list[dict], float]:
    """Returns (list of test results, total duration in seconds)."""
    root = ET.parse(path).getroot()
    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    tests = []
    duration = 0.0
    for suite in suites:
        duration += float(suite.get("time", "0"))
        for case in suite.iter("testcase"):
            name = case.get("name", "?")
            module = case.get("classname", "").split(".")[-1]
            tests.append({"name": f"{module}::{name}" if module else name,
                          "outcome": _outcome(case),
                          "duration_s": round(float(case.get("time", "0")), 3)})
    return tests, round(duration, 1)


def read_metrics(metrics_dir: str) -> dict:
    metrics = {}
    if not os.path.isdir(metrics_dir):
        return metrics
    for fname in sorted(os.listdir(metrics_dir)):
        if fname.endswith(".json"):
            with open(os.path.join(metrics_dir, fname), encoding="utf-8") as f:
                entry = json.load(f)
            metrics[entry["name"]] = {k: entry[k] for k in ("value", "unit", "meaning", "target")
                                      if k in entry}
    return metrics


def build_summary(res_dir: str) -> dict:
    junit = os.path.join(res_dir, "junit.xml")
    errors_extra = 0
    tests: list[dict] = []
    duration = 0.0
    if os.path.isfile(junit):
        tests, duration = read_junit(junit)
    else:
        errors_extra = 1  # pytest did not even produce a report
    count = {o: sum(1 for t in tests if t["outcome"] == o)
             for o in ("passed", "failed", "error", "skipped")}
    return {"schema": 1, "kernel": os.uname().release,
            "passed": count["passed"], "failed": count["failed"],
            "errors": count["error"] + errors_extra, "skipped": count["skipped"],
            "duration_s": duration, "tests": tests,
            "metrics": read_metrics(os.path.join(res_dir, "metrics"))}


if __name__ == "__main__":
    print(json.dumps(build_summary(sys.argv[1]), separators=(",", ":")))
