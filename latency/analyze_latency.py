#!/usr/bin/env python3
"""
Compare one-way uplink latency across runs with different numbers of UEs.

    python3 analyze_latency.py --run N1=run_N1.csv --run N2=run_N2.csv [--warmup 10]

Outputs (current directory):
    latency_summary.txt     per-run and per-UE stats (mean / P50 / P95 / P99 / std / max)
    latency_cdf.png         CDF per run (all UEs pooled) + per UE
    latency_vs_n.png        mean and P99 vs. number of UEs - the real data behind the
                            "latency vs N" curve (baseline, no AASC control)

--warmup drops the first K samples of each UE (connection setup / initial
scheduling transients).
"""
import argparse

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def stats(s):
    return {
        "n": len(s),
        "mean": s.mean(),
        "p50": s.quantile(0.50),
        "p95": s.quantile(0.95),
        "p99": s.quantile(0.99),
        "std": s.std(),
        "max": s.max(),
    }


def fmt(d):
    return (f"n={d['n']:5d}  mean={d['mean']:8.2f}  p50={d['p50']:8.2f}  "
            f"p95={d['p95']:8.2f}  p99={d['p99']:8.2f}  std={d['std']:7.2f}  max={d['max']:8.2f}  (ms)")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--run", action="append", required=True,
                   help="LABEL=file.csv, e.g. N1=run_N1.csv (repeatable)")
    p.add_argument("--warmup", type=int, default=10)
    args = p.parse_args()

    runs = {}
    for spec in args.run:
        label, path = spec.split("=", 1)
        df = pd.read_csv(path)
        df = df.sort_values(["ue_id", "seq"]).reset_index(drop=True)
        # drop the first --warmup samples of each UE
        df = df[df.groupby("ue_id").cumcount() >= args.warmup]
        runs[label] = df

    lines = []
    for label, df in runs.items():
        lines.append(f"=== run {label}: {df['ue_id'].nunique()} UE(s) ===")
        lines.append("  all UEs pooled: " + fmt(stats(df["latency_ms"])))
        for ue, g in df.groupby("ue_id"):
            lines.append(f"  ue{ue:<3}         : " + fmt(stats(g["latency_ms"])))
        lines.append("")
    text = "\n".join(lines)
    print(text)
    open("latency_summary.txt", "w").write(text)

    # CDF
    fig, ax = plt.subplots(figsize=(9, 5))
    for label, df in runs.items():
        x = np.sort(df["latency_ms"].values)
        y = np.arange(1, len(x) + 1) / len(x)
        ax.plot(x, y, label=f"{label} (all UEs)", linewidth=2)
        if df["ue_id"].nunique() > 1:
            for ue, g in df.groupby("ue_id"):
                xu = np.sort(g["latency_ms"].values)
                yu = np.arange(1, len(xu) + 1) / len(xu)
                ax.plot(xu, yu, linestyle="--", alpha=0.6, label=f"{label} ue{ue}")
    ax.set_xlabel("one-way uplink latency (ms)")
    ax.set_ylabel("CDF")
    ax.set_title("Uplink latency CDF (baseline, no AASC control)")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig("latency_cdf.png", dpi=150)
    print("wrote latency_cdf.png")

    # mean / p99 vs number of UEs
    rows = []
    for label, df in runs.items():
        s = stats(df["latency_ms"])
        rows.append((df["ue_id"].nunique(), label, s["mean"], s["p99"]))
    rows.sort()
    if len(rows) >= 2:
        fig, ax = plt.subplots(figsize=(7, 5))
        n = [r[0] for r in rows]
        ax.plot(n, [r[2] for r in rows], "o-", label="mean")
        ax.plot(n, [r[3] for r in rows], "s-", label="P99")
        for r in rows:
            ax.annotate(r[1], (r[0], r[2]), textcoords="offset points", xytext=(5, 5))
        ax.set_xlabel("number of UEs")
        ax.set_ylabel("latency (ms)")
        ax.set_title("Measured uplink latency vs. UE count (baseline)")
        ax.set_xticks(n)
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig("latency_vs_n.png", dpi=150)
        print("wrote latency_vs_n.png")


if __name__ == "__main__":
    main()
