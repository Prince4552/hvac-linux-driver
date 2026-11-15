# SPDX-License-Identifier: MIT
"""Checks the simulator results against the Phase 5 targets (PROJECT_PLAN.md section 7, Phase 5).

Rules: the product controller has 0 short-cycle violations and comfort >= 98 % in every scenario,
and fewer compressor starts per hour than the naive baseline on the hot day.
Reads results/sil/sil_metrics.json, writes results/host/sil_check.json, exits 1 on any failure.
Usage: check_sil.py SIL_METRICS_JSON OUT_JSON
"""
import json
import sys

MIN_COMFORT_PCT = 98.0


def check(runs: list[dict]) -> list[str]:
    """Returns a list of human-readable failures (empty when every rule holds)."""
    by_key = {(r["scenario"], r["controller"]): r for r in runs}
    failures = []
    for (scenario, controller), r in sorted(by_key.items()):
        if controller != "product":
            continue
        if r["short_cycle_violations"] != 0:
            failures.append(f"{scenario}: product short_cycle_violations = "
                            f"{r['short_cycle_violations']} (target 0)")
        if r["comfort_pct"] < MIN_COMFORT_PCT:
            failures.append(f"{scenario}: product comfort_pct = {r['comfort_pct']} "
                            f"(target >= {MIN_COMFORT_PCT})")
    product = by_key.get(("hot_day", "product"))
    baseline = by_key.get(("hot_day", "baseline"))
    if product is None or baseline is None:
        failures.append("hot_day product or baseline run missing")
    elif product["compressor_starts_per_hour"] >= baseline["compressor_starts_per_hour"]:
        failures.append("hot_day: product starts per hour not lower than baseline")
    return failures


def main(metrics_path: str, out_path: str) -> int:
    with open(metrics_path, encoding="utf-8") as f:
        runs = json.load(f)["runs"]
    failures = check(runs)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"schema": 1, "checks_failed": len(failures), "failures": failures,
                   "runs": runs}, f, indent=1)
    for line in failures:
        print(f"[sil-check] FAIL: {line}")
    print(f"[sil-check] {'PASS' if not failures else 'FAILED'} ({len(runs)} runs)")
    return 0 if not failures else 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
