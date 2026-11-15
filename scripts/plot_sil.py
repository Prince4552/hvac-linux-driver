# SPDX-License-Identifier: MIT
"""Plots the first 4 simulated hours of the hot day: product controller vs naive baseline.

Each panel shows the true room temperature, the four switching thresholds and shading where the
compressor is on. Writes docs/images/sil_hot_day.png. Runs in the host venv (matplotlib).
Usage: plot_sil.py SIL_DIR OUT_PNG
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")  # draw to a file; no display needed
import matplotlib.pyplot as plt  # noqa: E402

HOURS = 4
THRESHOLDS_C = {"heating starts": 19.0, "heating stops": 21.0,
                "cooling stops": 25.0, "cooling starts": 27.0}


def read_run(path: str) -> tuple[list[float], list[float], list[int]]:
    """Returns (time in hours, true temperature in C, compressor 0/1) for the first HOURS."""
    hours, temps, comp = [], [], []
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            t_h = int(row["t_s"]) / 3600.0
            if t_h > HOURS:
                break
            hours.append(t_h)
            temps.append(int(row["true_mc"]) / 1000.0)
            comp.append(int(row["compressor"]))
    return hours, temps, comp


def draw_panel(ax, title: str, run: tuple[list[float], list[float], list[int]]) -> None:
    hours, temps, comp = run
    ax.fill_between(hours, 0, 1, where=[c == 1 for c in comp], step="post",
                    transform=ax.get_xaxis_transform(), color="tab:blue", alpha=0.15,
                    label="compressor on")
    ax.plot(hours, temps, color="black", linewidth=1.0, label="room temperature")
    for label, value in THRESHOLDS_C.items():
        ax.axhline(value, linestyle="--", linewidth=0.8,
                   color="tab:red" if "cool" in label else "tab:orange")
        ax.text(HOURS, value, f" {label} ({value:g} °C)", va="center", fontsize=7)
    ax.set_title(title, fontsize=10)
    ax.set_ylabel("°C")
    ax.set_ylim(18, 31)
    ax.legend(loc="upper right", fontsize=7)


def main(sil_dir: str, out_png: str) -> int:
    fig, axes = plt.subplots(2, 1, figsize=(10, 6.5), sharex=True)
    draw_panel(axes[0], "hvac_core (deadband + minimum run/off timers)",
               read_run(f"{sil_dir}/hot_day_product.csv"))
    draw_panel(axes[1], "naive baseline (on above 26 °C, off below 26 °C)",
               read_run(f"{sil_dir}/hot_day_baseline.csv"))
    axes[1].set_xlabel("simulated time (hours from midnight), hot day: 35 °C outside")
    fig.tight_layout()
    fig.subplots_adjust(right=0.86)  # room for the threshold labels
    fig.savefig(out_png, dpi=90)
    print(f"[plot] wrote {out_png}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
