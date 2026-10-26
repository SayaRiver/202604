#!/usr/bin/env python3
#
# plot_p99.py  -  read all latency_N*.csv, print per-N stats, draw P99-vs-N curve.
#
# Usage:  python3 plot_p99.py            # auto-finds latency_N*.csv in current dir
#         python3 plot_p99.py --warmup 15   # drop first 15 s of each file
#
# Splits per UE (ue_id column) AND gives the combined per-N distribution.
# Drops warmup and overload spikes (>2000 ms) so rfsim-overload junk doesn't
# dominate the percentiles -- report both "clean" and raw counts so you stay honest.
 
import csv, glob, re, argparse, statistics as st
 
def pct(v, p):
    if not v: return float('nan')
    v = sorted(v); k = (len(v)-1)*p/100.0; lo=int(k); hi=min(lo+1,len(v)-1)
    return v[lo] + (v[hi]-v[lo])*(k-lo)
 
ap = argparse.ArgumentParser()
ap.add_argument("--warmup", type=float, default=10.0)
ap.add_argument("--cap", type=float, default=2000.0, help="drop samples above this (ms) as overload")
args = ap.parse_args()
 
files = sorted(glob.glob("latency_N*.csv"),
               key=lambda s: int(re.search(r'N(\d+)', s).group(1)))
if not files:
    print("no latency_N*.csv found here"); raise SystemExit
 
Ns, p50s, p99s = [], [], []
print(f"{'N':>3} {'clean_n':>8} {'dropped':>8} {'mean':>8} {'p50':>8} {'p95':>8} {'p99':>8} {'max':>9}")
for fn in files:
    N = int(re.search(r'N(\d+)', fn).group(1))
    rows = list(csv.DictReader(open(fn)))
    if not rows: continue
    t0 = min(float(r['recv_ts']) for r in rows)
    lat = [float(r['latency_ms']) for r in rows
           if float(r['recv_ts']) - t0 >= args.warmup]
    clean = [x for x in lat if 0 < x < args.cap]
    dropped = len(lat) - len(clean)
    if not clean: 
        print(f"{N:>3}  no clean samples"); continue
    Ns.append(N); p50s.append(pct(clean,50)); p99s.append(pct(clean,99))
    print(f"{N:>3} {len(clean):>8} {dropped:>8} {sum(clean)/len(clean):>8.1f} "
          f"{pct(clean,50):>8.1f} {pct(clean,95):>8.1f} {pct(clean,99):>8.1f} {max(clean):>9.1f}")
 
    # per-UE breakdown if the file has ue_id
    if rows and 'ue_id' in rows[0]:
        by = {}
        for r in rows:
            if float(r['recv_ts'])-t0 < args.warmup: continue
            x=float(r['latency_ms'])
            if 0<x<args.cap: by.setdefault(r['ue_id'],[]).append(x)
        for ue,xs in by.items():
            print(f"      └ {ue}: n={len(xs)} p50={pct(xs,50):.1f} p99={pct(xs,99):.1f} ms")
 
# plot
try:
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(8,5))
    ax.plot(Ns, p50s, "o-", label="p50 (median)")
    ax.plot(Ns, p99s, "s-", label="p99 (tail)", color="#d64545")
    ax.set_xlabel("number of UEs (N)"); ax.set_ylabel("uplink latency (ms)")
    ax.set_title("Uplink latency vs N (clean samples)")
    ax.set_xticks(Ns); ax.grid(True, alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig("p99_vs_n.png", dpi=150)
    print("\nwrote p99_vs_n.png")
except Exception as e:
    print(f"\n(plot skipped: {e})")
 
