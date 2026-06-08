#!/usr/bin/env python3
"""Analyse v2 PrediCache experiment CSVs (with replicas)."""
import re
import statistics
import sys
from pathlib import Path

WARMUP = 5  # seconds to skip
RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/main_v2")


def parse(path: Path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.rstrip()
            if "buffer-pool-size" in line or line.startswith("space:"):
                continue
            if line.startswith("ts,tx") or line.startswith("cbp_class"):
                continue
            parts = line.split(",")
            if len(parts) < 11:
                continue
            try:
                ts = int(parts[0])
                tx = int(parts[1])
                rows.append((ts, tx))
            except ValueError:
                continue
    if not rows:
        return None
    after_warmup = [tx for ts, tx in rows if ts >= WARMUP]
    if not after_warmup:
        return None
    med = statistics.median(after_warmup)
    in_mem = [tx for tx in after_warmup if tx >= 0.10 * med]
    if not in_mem:
        in_mem = after_warmup
    return statistics.mean(in_mem)


def main():
    pat = re.compile(r"^(?P<wl>\w+?)_(?P<cfg>chance\d+|cbp)_r(?P<rep>\d+)\.csv$")
    grouped = {}
    for p in sorted(RESULTS.glob("*.csv")):
        m = pat.match(p.name)
        if not m:
            continue
        wl, cfg, rep = m["wl"], m["cfg"], int(m["rep"])
        mean_tx = parse(p)
        if mean_tx is None:
            continue
        grouped.setdefault(wl, {}).setdefault(cfg, []).append(mean_tx)

    for wl in ("tpcc", "rndread", "ycsb_skewed"):
        if wl not in grouped:
            continue
        print(f"\n== Workload: {wl} ==")
        cfgs = grouped[wl]
        order = {"chance0": 0, "chance4": 1, "chance16": 2, "chance32": 3,
                 "chance64": 3.5, "chance128": 4, "chance1024": 5, "cbp": 6}
        items = sorted(cfgs.items(), key=lambda kv: order.get(kv[0], 99))
        baseline = cfgs.get("chance32")
        baseline_mean = statistics.mean(baseline) if baseline else None
        best_mean = max(statistics.mean(v) for v in cfgs.values())
        print(f"  {'config':12s}  {'mean TX/s':>12s}  {'min':>12s}  {'max':>12s}  {'reps':>4s}  vs ch32     ")
        for cfg, vals in items:
            mean = statistics.mean(vals)
            note = "  <-- best" if abs(mean - best_mean) < 1 else ""
            delta = ""
            if baseline_mean and cfg != "chance32":
                pct = (mean - baseline_mean) / baseline_mean * 100
                delta = f"{pct:+.1f}%"
            print(f"  {cfg:12s}  {mean:12.0f}  {min(vals):12.0f}  {max(vals):12.0f}  {len(vals):>4d}  {delta:>9s}  {note}")


if __name__ == "__main__":
    main()
