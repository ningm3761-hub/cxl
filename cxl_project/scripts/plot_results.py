#!/usr/bin/env python3
import os
import pandas as pd
import matplotlib.pyplot as plt

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(BASE_DIR)
RESULT_DIR = os.path.join(PROJECT_DIR, "results")
CSV_PATH = os.environ.get("CSV_PATH", os.path.join(RESULT_DIR, "results_numa_clean.csv"))

df = pd.read_csv(CSV_PATH)

# If multiple seeds exist, average by track/load.
agg = (
    df.groupby(["track", "load"], as_index=False)
      .agg(
          goodput=("goodput", "mean"),
          delay_p99=("delay_p99", "mean"),
          delay_p50=("delay_p50", "mean"),
          delay_p95=("delay_p95", "mean"),
      )
)

order = ["random", "csma", "aimd"]
labels = {"random": "RANDOM", "csma": "CSMA-like", "aimd": "AIMD"}

plt.figure(figsize=(8, 5))
for mode in order:
    sub = agg[agg["track"] == mode].sort_values("load")
    if not sub.empty:
        plt.plot(sub["load"], sub["goodput"], marker="o", linewidth=2, label=labels[mode])

plt.xlabel("Load (%)")
plt.ylabel("Goodput (requests/s)")
plt.title("Load vs Goodput")
plt.grid(True, linestyle="--", alpha=0.5)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(RESULT_DIR, "goodput_vs_load.png"), dpi=300)

plt.figure(figsize=(8, 5))
for mode in order:
    sub = agg[agg["track"] == mode].sort_values("load")
    if not sub.empty:
        plt.plot(sub["load"], sub["delay_p99"] / 1e6, marker="o", linewidth=2, label=labels[mode])

plt.xlabel("Load (%)")
plt.ylabel("p99 End-to-End Request Latency (ms)")
plt.title("Load vs p99 End-to-End Request Latency")
plt.grid(True, linestyle="--", alpha=0.5)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(RESULT_DIR, "p99_vs_load.png"), dpi=300)

print(f"Saved figures to {RESULT_DIR}")
