#!/usr/bin/env python3
"""
Offline analysis of sample_nats_analysis.csv (see sample_nats.c CSV logging).

Usage:
    python3 analyze_results.py sample_nats_analysis.csv [--ground-truth-sr-period-ms 3000]

Produces (in the current directory):
    - accuracy_summary.txt   : text summary of prediction error stats
    - sr_period_over_time.png: alg1 vs alg2 predicted sr_period vs known ground truth
    - burst_response.png     : how sr_period/bsr_size shift around burst events

This deliberately does NOT try to draw conclusions for you - it produces the
numbers and plots; interpreting them (e.g. "does the CV-based algorithm
converge faster?") is the actual analysis you write up.
"""
import sys
import argparse

import pandas as pd
import matplotlib
matplotlib.use("Agg")  # no display needed, just save PNGs
import matplotlib.pyplot as plt


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv_path")
    p.add_argument("--ground-truth-sr-period-ms", type=float, default=3000.0,
                    help="Known true publish interval (pubx.py type 2 = 3000ms)")
    args = p.parse_args()

    df = pd.read_csv(args.csv_path)
    df = df.sort_values("timestamp").reset_index(drop=True)
    df["t0"] = df["timestamp"] - df["timestamp"].iloc[0]  # seconds since first row

    gt = args.ground_truth_sr_period_ms

    # --- 1. Prediction accuracy vs known ground truth ---------------------
    # Only meaningful once each algorithm has actually locked onto a
    # "Periodic" classification (sr_period == 0 rows are Continuous/Unknown
    # and not comparable to a fixed period).
    periodic1 = df[df["alg1_type"] == "Periodic"].copy()
    periodic2 = df[df["alg2_type"] == "Periodic"].copy()

    periodic1["error_ms"] = (periodic1["alg1_sr_period_ms"] - gt).abs()
    periodic2["error_ms"] = (periodic2["alg2_sr_period_ms"] - gt).abs()

    summary_lines = []
    summary_lines.append(f"Rows total: {len(df)}")
    summary_lines.append(f"Ground truth sr_period: {gt} ms\n")

    for name, sub in [("alg1 (file1, median/range)", periodic1),
                       ("alg2 (file2, jitter/CV)", periodic2)]:
        if len(sub) == 0:
            summary_lines.append(f"{name}: no Periodic rows found\n")
            continue
        summary_lines.append(f"{name}: n={len(sub)}")
        summary_lines.append(f"  mean error: {sub['error_ms'].mean():.3f} ms")
        summary_lines.append(f"  std error:  {sub['error_ms'].std():.3f} ms")
        summary_lines.append(f"  max error:  {sub['error_ms'].max():.3f} ms")
        summary_lines.append(f"  RMSE:       {(sub['error_ms']**2).mean()**0.5:.3f} ms\n")

    # --- 2. Burst detection agreement between the two algorithms ----------
    both_burst_cols = df[["alg1_has_burst", "alg2_has_burst"]].astype(bool)
    agree = (both_burst_cols["alg1_has_burst"] == both_burst_cols["alg2_has_burst"]).mean()
    summary_lines.append(f"has_burst agreement between alg1/alg2: {agree*100:.1f}% of rows")
    summary_lines.append(
        f"  alg1 flagged burst on {both_burst_cols['alg1_has_burst'].mean()*100:.1f}% of rows, "
        f"alg2 on {both_burst_cols['alg2_has_burst'].mean()*100:.1f}%"
        " (a gap here is expected - see the algorithms' different has_burst definitions)"
    )

    with open("accuracy_summary.txt", "w") as f:
        f.write("\n".join(summary_lines))
    print("\n".join(summary_lines))
    print("\nWrote accuracy_summary.txt")

    # --- 3. sr_period over time, both algorithms vs ground truth ----------
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(periodic1["t0"], periodic1["alg1_sr_period_ms"], "o-", label="alg1 (median/range)", alpha=0.7)
    ax.plot(periodic2["t0"], periodic2["alg2_sr_period_ms"], "s-", label="alg2 (jitter/CV)", alpha=0.7)
    ax.axhline(gt, color="black", linestyle="--", label=f"ground truth ({gt:.0f} ms)")
    ax.set_xlabel("time since first sample (s)")
    ax.set_ylabel("predicted sr_period (ms)")
    ax.set_title("Predicted SR period vs. known ground truth, over time")
    ax.legend()
    fig.tight_layout()
    fig.savefig("sr_period_over_time.png", dpi=150)
    print("Wrote sr_period_over_time.png")

    # --- 4. Burst response: sr_period / bsr_size around burst events ------
    burst_rows = df[(df["alg1_has_burst"] == 1) | (df["alg2_has_burst"] == 1)]
    if len(burst_rows) > 0:
        fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
        axes[0].plot(df["t0"], df["alg1_bsr_size"], label="alg1 bsr_size", alpha=0.6)
        axes[0].plot(df["t0"], df["alg2_bsr_size"], label="alg2 bsr_size", alpha=0.6)
        axes[0].plot(df["t0"], df["alg2_optimized_bsr_size"], label="alg2 optimized_bsr_size", alpha=0.6)
        axes[0].scatter(burst_rows["t0"], burst_rows["alg2_optimized_bsr_size"],
                         color="red", zorder=5, label="burst-flagged rows")
        axes[0].set_ylabel("bsr_size (bytes)")
        axes[0].legend()

        axes[1].plot(df["t0"], df["alg2_sr_period_ms"], label="alg2 sr_period", alpha=0.6)
        axes[1].plot(df["t0"], df["alg2_optimized_sr_period_ms"], label="alg2 optimized_sr_period", alpha=0.6)
        axes[1].scatter(burst_rows["t0"], burst_rows["alg2_optimized_sr_period_ms"],
                         color="red", zorder=5, label="burst-flagged rows")
        axes[1].set_xlabel("time since first sample (s)")
        axes[1].set_ylabel("sr_period (ms)")
        axes[1].legend()

        fig.suptitle("BSR/SR behavior around burst (QoS2) events")
        fig.tight_layout()
        fig.savefig("burst_response.png", dpi=150)
        print("Wrote burst_response.png")
    else:
        print("No burst-flagged rows in this dataset - skipping burst_response.png")


if __name__ == "__main__":
    main()
