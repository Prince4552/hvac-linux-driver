# SPDX-License-Identifier: MIT
"""Merges results/host/*.json and results/guest/*.json into results/metrics.json and renders
results/METRICS.md (PROJECT_PLAN.md sections 4.9 and 9). Each metric has a value, unit, target, a
pass/fail verdict and a plain-English meaning. Exits 1 if any metric misses its target or is missing.
Usage: collect_metrics.py RESULTS_DIR
"""
import datetime
import json
import os
import subprocess
import sys

REPORT = "report"  # target for numbers that are reported, not judged


def load(path: str) -> dict:
    if not os.path.isfile(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def meets(value: object, target: str) -> bool:
    """Evaluates simple targets such as '== 0', '>= 500', '<= 1.6', '< 592.2', '== 3/3'."""
    if target == REPORT:
        return value is not None
    op, _, expected = target.partition(" ")
    if isinstance(value, str) or "/" in expected:
        return op == "==" and str(value) == expected
    if value is None:
        return False
    v, e = float(value), float(expected)
    return {"==": v == e, ">=": v >= e, "<=": v <= e, "<": v < e, ">": v > e}[op]


class Collector:
    def __init__(self) -> None:
        self.metrics: dict[str, dict] = {}

    def add(self, mid: str, value: object, unit: str, target: str, meaning: str,
            source: str) -> None:
        self.metrics[mid] = {"value": value, "unit": unit, "target": target,
                             "pass": meets(value, target), "meaning": meaning, "source": source}


def add_test_counts(c: Collector, host: dict, guest: dict) -> None:
    c.add("guest_tests_passed", guest.get("passed"), "count", REPORT,
          "In-guest tests that passed on the emulated board", "make test-qemu, summary.json")
    c.add("guest_tests_failed", guest.get("failed", 0) + guest.get("errors", 0)
          + guest.get("skipped", 0) if guest else None, "count", "== 0",
          "In-guest tests that failed, errored or were skipped", "make test-qemu, summary.json")
    c.add("host_tests_passed", host.get("host_tests_passed"), "count", REPORT,
          "Unit, integration and simulation checks on the host", "make test-host")
    c.add("host_tests_failed", host.get("host_tests_failed"), "count", "== 0",
          "Host checks that failed", "make test-host")


def add_driver(c: Collector, gm: dict, static: dict, mutation: dict) -> None:
    def g(name: str) -> object:
        return gm.get(name, {}).get("value")
    src4 = "phase 4, test_driver_robustness.py / test_memory.py"
    c.add("kunit_cases_passed", g("kunit_cases_passed"), "count", "== 5",
          "Kernel-level checks of the conversion math (all KUnit cases)", "phase 3, test_kunit.py")
    c.add("driver_load_unload_cycles", g("driver_load_unload_cycles"), "count", ">= 500",
          "Times the driver was loaded and removed without a problem", src4)
    c.add("kmemleak_reports", g("kmemleak_reports"), "count", "== 0",
          "Memory blocks our driver allocated and never freed", src4)
    c.add("kasan_reports", g("kasan_reports"), "count", "== 0",
          "Invalid memory accesses (use-after-free, out of bounds)", src4)
    c.add("lockdep_reports", g("lockdep_reports"), "count", "== 0",
          "Locking mistakes that could deadlock", src4)
    c.add("concurrent_read_errors", g("concurrent_read_errors"), "count", "== 0",
          "Wrong or failed readings with 8 threads reading at once", src4)
    for p in ("median", "p99"):
        c.add(f"driver_read_latency_{p}_us", g(f"driver_read_latency_{p}_us"), "us", REPORT,
              f"Time for one temperature read on the emulated board ({p}; relative only)", src4)
    c.add("checkpatch_errors", static.get("checkpatch_errors"), "count", "== 0",
          "Kernel coding-style errors", "phase 4, static_analysis.sh")
    c.add("checkpatch_warnings", static.get("checkpatch_warnings"), "count", "== 0",
          "Kernel coding-style warnings (allowed types excluded)", "phase 4, static_analysis.sh")
    c.add("sparse_warnings", static.get("sparse_warnings"), "count", "== 0",
          "Kernel type-checking problems", "phase 4, static_analysis.sh")
    c.add("mutation_score", mutation.get("mutation_score"), "killed/total", "== 3/3",
          "Deliberately inserted bugs that the tests caught", "phase 4, run_mutation.sh")


def add_user_space(c: Collector, d: str) -> None:
    cpp = load(f"{d}/host/cppcheck.json")
    cov = load(f"{d}/host/coverage.json")
    san = load(f"{d}/host/sanitizers.json")
    vg = load(f"{d}/host/valgrind.json")
    c.add("cppcheck_findings", cpp.get("cppcheck_findings"), "count", "== 0",
          "Likely bugs in user-space C found without running it", "phase 5, test_host.sh")
    c.add("core_line_coverage_pct", cov.get("core_line_coverage_pct"), "%", ">= 95",
          "Share of control-core lines exercised by unit tests", "phase 5, gcovr")
    c.add("core_branch_coverage_pct", cov.get("core_branch_coverage_pct"), "%", ">= 90",
          "Share of control-core branches exercised by unit tests", "phase 5, gcovr")
    c.add("sanitizer_errors", san.get("sanitizer_errors"), "count", "== 0",
          "Memory and undefined-behavior errors found by ASan/UBSan", "phase 5, test_host.sh")
    c.add("valgrind_errors", vg.get("valgrind_errors"), "count", "== 0",
          "Memory errors found by Valgrind", "phase 5, test_host.sh")
    c.add("valgrind_leaked_bytes", vg.get("valgrind_leaked_bytes"), "bytes", "== 0",
          "Memory leaked (definitely or indirectly lost) in user-space code", "phase 5")


def add_sil(c: Collector, d: str) -> None:
    runs = {(r["scenario"], r["controller"]): r
            for r in load(f"{d}/sil/sil_metrics.json").get("runs", [])}
    for scenario in ("hot_day", "cold_day", "mild_day"):
        prod = runs.get((scenario, "product"), {})
        base = runs.get((scenario, "baseline"), {})
        src = "phase 5, sil"
        c.add(f"sil_comfort_pct_{scenario}", prod.get("comfort_pct"), "%", ">= 98",
              "Share of time the simulated room stayed within 18.5..27.5 C", src)
        b = base.get("compressor_starts_per_hour")
        # With no cooling demand both are 0 and "fewer" cannot apply (D-026).
        target = f"< {b}" if b else "== 0"
        c.add(f"sil_starts_per_hour_{scenario}_product", prod.get("compressor_starts_per_hour"),
              "per hour", target, "How often the compressor starts (fewer is gentler)", src)
        c.add(f"sil_starts_per_hour_{scenario}_baseline", b, "per hour", REPORT,
              "Compressor starts per hour of the naive baseline controller", src)
        c.add(f"sil_short_cycle_violations_{scenario}", prod.get("short_cycle_violations"),
              "count", "== 0", "Compressor restarts that came too soon", src)


def add_daemon_and_platform(c: Collector, gm: dict, build: dict) -> None:
    def g(name: str) -> object:
        return gm.get(name, {}).get("value")
    src6 = "phase 6, test_daemon_e2e.py"
    c.add("modbus_roundtrip_median_ms", g("modbus_roundtrip_median_ms"), "ms", REPORT,
          "Time for a Modbus request and reply on the emulated board", src6)
    c.add("fault_detection_latency_s", g("fault_detection_latency_s"), "s", "<= 1.6",
          "Time from sensor loss to alarm visible over Modbus (target 3 x 0.2 s + 1 s)", src6)
    c.add("hvacd_rss_kb", g("hvacd_rss_kb"), "kB", REPORT, "Daemon memory use after 30 s", src6)
    c.add("hvacd_binary_bytes", g("hvacd_binary_bytes"), "bytes", REPORT,
          "Daemon program size (stripped)", src6)
    c.add("guest_boot_to_tests_s", g("guest_boot_to_tests_s"), "s", REPORT,
          "Seconds from kernel start to the start of the guest tests", "phase 1")
    c.add("build_duration_min", build.get("full_build_duration_min"), "min", REPORT,
          "Duration of the last full (from empty output folder) image build", "build.sh")
    c.add("rootfs_size_mb", build.get("rootfs_size_mb"), "MB", REPORT,
          "Space used by the root filesystem image", "build.sh")


def add_extensions(c: Collector, d: str, gm: dict) -> None:
    """Extension metrics appear once their phase has produced them (PROJECT_PLAN.md section 8)."""
    ext = load(f"{d}/host/extensions.json").get("metrics", {})
    for name, m in sorted({**gm, **ext}.items()):
        if name in c.metrics or "target" not in m:
            continue
        c.add(name, m["value"], m["unit"], m["target"], m["meaning"], m.get("source", "extension"))


def git_commit() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def render_markdown(data: dict) -> str:
    lines = ["# Metrics", "",
             f"Generated {data['generated_utc']} from commit `{data['git_commit']}` by "
             "`make metrics`. Timing numbers come from an emulated ARM64 board (QEMU without "
             "hardware acceleration) and are only meaningful relative to each other.", "",
             "| Metric | Value | Target | Pass | What it means |", "| --- | --- | --- | --- | --- |"]
    for mid, m in data["metrics"].items():
        value = "-" if m["value"] is None else f"{m['value']} {m['unit']}"
        lines.append(f"| `{mid}` | {value} | {m['target']} | {'yes' if m['pass'] else '**NO**'} "
                     f"| {m['meaning']} |")
    return "\n".join(lines) + "\n"


def main(d: str) -> int:
    gm = load(f"{d}/guest/guest_metrics.json").get("metrics", {})
    c = Collector()
    add_test_counts(c, load(f"{d}/host/host_summary.json"), load(f"{d}/guest/summary.json"))
    add_driver(c, gm, load(f"{d}/host/static_analysis.json"), load(f"{d}/host/mutation.json"))
    add_user_space(c, d)
    add_sil(c, d)
    add_daemon_and_platform(c, gm, load(f"{d}/host/build_info.json"))
    add_extensions(c, d, gm)
    data = {"schema": 1,
            "generated_utc": datetime.datetime.now(datetime.timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ"),
            "git_commit": git_commit(), "metrics": c.metrics}
    with open(f"{d}/metrics.json", "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    with open(f"{d}/METRICS.md", "w", encoding="utf-8") as f:
        f.write(render_markdown(data))
    failing = [mid for mid, m in c.metrics.items() if not m["pass"]]
    print(f"[metrics] {len(c.metrics)} metrics, {len(failing)} missing their target")
    for mid in failing:
        print(f"[metrics] FAIL: {mid} = {c.metrics[mid]['value']} (target {c.metrics[mid]['target']})")
    return 1 if failing else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
