# SPDX-License-Identifier: MIT
"""Checks the Ext 4 equivalence test: the FreeRTOS trace must be byte-identical to the host
simulator's trace (same scenario, seed and length; D-030), with no queue send failures. Adds the
FreeRTOS metrics to results/host/extensions.json (read by collect_metrics.py). Exits 1 on failure.
Usage: check_rtos.py HOST_TRACE RTOS_TRACE RTOS_METRICS_JSON EXTENSIONS_JSON
"""
import json
import sys

from check_fdd import merge_metrics

SRC = "ext 4, hvac_rtos"
STACK_MEANING = ("Unused words of the {task} task's FreeRTOS stack (POSIX simulator: covers only "
                 "the FreeRTOS stack area, not the Linux thread's stack; D-030)")


def match_pct(host: bytes, rtos: bytes) -> float:
    """100.0 only if the files are byte-identical; otherwise the share of equal lines."""
    if host == rtos:
        return 100.0
    a, b = host.splitlines(), rtos.splitlines()
    same = sum(1 for x, y in zip(a, b) if x == y)
    return round(100.0 * same / max(len(a), len(b), 1), 2)


def main(host_path: str, rtos_path: str, metrics_path: str, ext_path: str) -> int:
    with open(host_path, "rb") as f:
        host = f.read()
    with open(rtos_path, "rb") as f:
        rtos = f.read()
    with open(metrics_path, encoding="utf-8") as f:
        m = json.load(f)
    pct = match_pct(host, rtos)
    failures = m["rtos_queue_send_failures"]
    new = {
        "rtos_trace_match_pct": {"value": pct, "unit": "%", "target": "== 100", "source": SRC,
                                 "meaning": "FreeRTOS trace identical to the host simulator's "
                                            "(hot_day, 2 h, seed 1)"},
        "rtos_queue_send_failures": {"value": failures, "unit": "count", "target": "== 0",
                                     "source": SRC,
                                     "meaning": "Queue sends that timed out in the FreeRTOS port"},
        "rtos_min_free_heap_bytes": {"value": m["rtos_min_free_heap_bytes"], "unit": "bytes",
                                     "target": "report", "source": SRC,
                                     "meaning": f"Lowest free FreeRTOS kernel heap (of "
                                                f"{m['rtos_heap_total_bytes']}; "
                                                f"{m['rtos_heap_allocations']} allocations)"},
    }
    for task, words in m["rtos_stack_high_water_words"].items():
        new[f"rtos_stack_high_water_words_{task}"] = {
            "value": words, "unit": "words", "target": "report", "source": SRC,
            "meaning": STACK_MEANING.format(task=task)}
    merge_metrics(ext_path, new)
    ok = pct == 100.0 and failures == 0
    print(f"[rtos] trace match {pct} %, queue send failures {failures}, "
          f"lines {len(host.splitlines())}: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(*sys.argv[1:5]))
