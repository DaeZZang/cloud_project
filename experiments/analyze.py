#!/usr/bin/env python3
"""Analyse PrediCache experiment CSVs.

Each input CSV has the form (after a couple of header rows):
    ts,tx,rmb,wmb,system,threads,datasize,workload,batch,writeCount,readCount
We discard the first WARMUP seconds and average the rest.

Usage: analyze.py [results_dir]
"""
import csv
import os
import re
import statistics
import sys
from pathlib import Path

WARMUP = 5  # seconds to skip
RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/main")


def parse(path: Path):
    rows = []
    workload = None
    with open(path) as f:
        for line in f:
            line = line.rstrip()
            if "buffer-pool-size" in line or line.startswith("space:"):
                continue
            if line.startswith("ts,tx"):
                continue
            parts = line.split(",")
            if len(parts) < 11:
                continue
            try:
                ts = int(parts[0])
                tx = int(parts[1])
                rows.append((ts, tx))
                workload = parts[7]
            except ValueError:
                continue
    if not rows:
        return None
    after_warmup = [tx for ts, tx in rows if ts >= WARMUP]
    if not after_warmup:
        return None
    # Drop rows with throughput < 10% of median — those are OOM-transition outliers.
    med = statistics.median(after_warmup)
    in_mem = [tx for tx in after_warmup if tx >= 0.10 * med]
    if not in_mem:
        in_mem = after_warmup
    return {
        "workload": workload,
        "n_total": len(after_warmup),
        "n_inmem": len(in_mem),
        "mean": statistics.mean(in_mem),
        "median": statistics.median(in_mem),
        "stdev": statistics.stdev(in_mem) if len(in_mem) > 1 else 0.0,
    }


def main():
    pat = re.compile(r"^(?P<wl>\w+?)_(?P<cfg>chance\d+|cbp)\.csv$")
    results = {}
    for p in sorted(RESULTS.glob("*.csv")):
        m = pat.match(p.name)
        if not m:
            continue
        wl, cfg = m["wl"], m["cfg"]
        r = parse(p)
        if not r:
            print(f"  [skip] {p.name}: no data rows")
            continue
        results.setdefault(wl, {})[cfg] = r

    for wl, cfgs in results.items():
        print(f"\n== Workload: {wl} ==")
        rows = []
        for cfg, r in cfgs.items():
            rows.append((cfg, r["mean"], r["stdev"], r["n_inmem"]))
        # Order: chance0, chance4, chance16, chance32 (paper default), chance128, chance1024, cbp
        order = {"chance0": 0, "chance4": 1, "chance16": 2, "chance32": 3,
                 "chance128": 4, "chance1024": 5, "cbp": 6}
        rows.sort(key=lambda x: order.get(x[0], 99))
        baseline = next((r for r in rows if r[0] == "chance32"), None)
        baseline_mean = baseline[1] if baseline else None
        best = max(rows, key=lambda r: r[1])
        print(f"  {'config':12s}  {'mean TX/s':>12s}  {'stdev':>8s}  {'n':>3s}  vs chance32   note")
        for cfg, mean, sd, n in rows:
            delta = ""
            if baseline_mean and cfg != "chance32":
                pct = (mean - baseline_mean) / baseline_mean * 100
                delta = f"{pct:+.1f}%"
            note = "  <-- best" if cfg == best[0] else ""
            print(f"  {cfg:12s}  {mean:12.0f}  {sd:8.0f}  {n:>3d}  {delta:>10s}  {note}")

    return results


if __name__ == "__main__":
    main()
