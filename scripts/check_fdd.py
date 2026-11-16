# SPDX-License-Identifier: MIT
"""Checks the Ext 2 fault-detection experiments: detection rate 100 % on the detection matrix and
0 false alarms on the healthy runs. Adds the three FDD metrics to results/host/extensions.json
(read by collect_metrics.py). Exits 1 if a target is missed.
Usage: check_fdd.py FDD_METRICS_JSON EXTENSIONS_JSON
"""
import json
import os
import sys


def merge_metrics(path: str, new: dict) -> None:
    """Adds or replaces metrics in the shared extensions file (other extensions add theirs)."""
    data = {"schema": 1, "metrics": {}}
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    data["metrics"].update(new)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)


def main(fdd_path: str, ext_path: str) -> int:
    with open(fdd_path, encoding="utf-8") as f:
        fdd = json.load(f)
    src = "ext 2, sil fdd"
    merge_metrics(ext_path, {
        "fdd_detection_rate": {"value": fdd["fdd_detection_rate"], "unit": "%", "target": ">= 100",
                               "meaning": "Injected faults whose expected diagnostic bit was set "
                                          "(5 cases)", "source": src},
        "fdd_false_alarms": {"value": fdd["fdd_false_alarms"], "unit": "count", "target": "== 0",
                             "meaning": "Healthy simulated days (3 scenarios x 5 seeds) with any "
                                        "diagnostic bit set", "source": src},
        "fdd_mean_time_to_detect_min": {"value": fdd["fdd_mean_time_to_detect_min"],
                                        "unit": "min", "target": "report",
                                        "meaning": "Average time from fault to diagnostic bit",
                                        "source": src}})
    for case in fdd["detection"]:
        print(f"[fdd] {case['case']}: detected={case['detected']} "
              f"after {case['time_to_detect_min']} min, bits={case['bits']}")
    ok = fdd["fdd_detection_rate"] >= 100 and fdd["fdd_false_alarms"] == 0
    print(f"[fdd] detection {fdd['fdd_detection_rate']} %, false alarms {fdd['fdd_false_alarms']}: "
          f"{'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
