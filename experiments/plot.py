#!/usr/bin/env python3
"""Generate publication-quality plots for the CBP study."""
import json
import os
import re
import statistics
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import sys as _sys; _sys.path.insert(0, str(Path(__file__).parent))
from analyze_v2 import parse  # type: ignore

ROOT = Path("/lustre/s_zun212/TPC-C")
RESULTS = ROOT / "results" / "main_v2"
SCALING = ROOT / "results" / "cbp_v3"
V4_DIR  = ROOT / "results" / "cbp_v4"
OUT_DIR = ROOT / "results" / "figures"
OUT_DIR.mkdir(parents=True, exist_ok=True)


def collect(folder: Path):
    pat = re.compile(r"^(?P<wl>\w+?)_(?P<cfg>chance\d+|cbp|cbp_static|trad|default)(?:_r(?P<rep>\d+))?\.csv$")
    grouped = {}
    for p in sorted(folder.glob("*.csv")):
        if "dump" in p.name:
            continue
        m = pat.match(p.name)
        if not m:
            continue
        val = parse(p)
        if val is None:
            continue
        wl, cfg = m["wl"], m["cfg"]
        if isinstance(val, (int, float)):
            grouped.setdefault(wl, {}).setdefault(cfg, []).append(val)
        else:
            grouped.setdefault(wl, {}).setdefault(cfg, []).append(val["mean"])
    # Reduce to mean+stdev per cfg
    out = {}
    for wl, cfgs in grouped.items():
        out[wl] = {}
        for cfg, vals in cfgs.items():
            out[wl][cfg] = {
                "mean": sum(vals) / len(vals),
                "stdev": (sum((v - sum(vals)/len(vals))**2 for v in vals) / max(len(vals)-1, 1))**0.5,
                "n": len(vals),
            }
    return out


def fig_sensitivity(grouped):
    """Throughput vs fixed promotion probability per workload."""
    workloads = ["tpcc", "rndread", "ycsb_skewed"]
    pretty = {"tpcc": "TPC-C", "rndread": "Random Read", "ycsb_skewed": "Skewed YCSB (zipf=0.9)"}
    chance_map = {"chance0": "off", "chance16": "1/16", "chance32": "1/32", "chance128": "1/128"}
    chance_order = ["chance0", "chance16", "chance32", "chance128"]
    fig, axes = plt.subplots(1, 3, figsize=(13, 4))
    for ax, wl in zip(axes, workloads):
        if wl not in grouped:
            continue
        rows = grouped[wl]
        xs, ys, errs = [], [], []
        for c in chance_order:
            if c not in rows:
                continue
            xs.append(chance_map[c])
            ys.append(rows[c]["mean"] / 1000)
            errs.append(rows[c]["stdev"] / 1000)
        if xs:
            x_pos = list(range(len(xs)))
            ax.errorbar(x_pos, ys, yerr=errs, marker="o", linewidth=1.5,
                        markersize=8, capsize=4, color="C0", label="Fixed prob.")
            ax.set_xticks(x_pos)
            ax.set_xticklabels(xs)
            # mark default 1/32
            try:
                idx = xs.index("1/32")
                ax.axvline(idx, color="grey", linestyle=":", linewidth=1, alpha=0.7)
                ax.text(idx, max(ys)*1.005, "paper default", color="grey",
                        ha="center", fontsize=9)
            except ValueError:
                pass
        if "cbp" in rows:
            cbp = rows["cbp"]
            ax.axhline(cbp["mean"] / 1000, color="C1", linestyle="--", linewidth=2,
                       label=f"CBP v2 ({cbp['mean']/1000:.0f}k)")
        ax.set_xlabel("Promotion probability")
        ax.set_ylabel("Throughput (k TX/s)")
        ax.set_title(pretty.get(wl, wl))
        ax.grid(alpha=0.3, axis="y")
        ax.legend(fontsize=9)
    plt.tight_layout()
    fig.savefig(OUT_DIR / "fig_sensitivity.pdf", bbox_inches="tight")
    fig.savefig(OUT_DIR / "fig_sensitivity.png", dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {OUT_DIR/'fig_sensitivity.pdf'}")


def fig_v3_compare(grouped):
    """v3 ablation: bar chart of trad / default / cbp_static / cbp per workload."""
    workloads = ["tpcc", "rndread", "ycsb_skewed"]
    pretty = {"tpcc": "TPC-C", "rndread": "Random Read", "ycsb_skewed": "Skewed YCSB"}
    variants = ["trad", "default", "cbp_static", "cbp"]
    pretty_var = {"trad": "Traditional\n(no pred. trans.)", "default": "Default\n(1/32)",
                  "cbp_static": "CBP-static\n(observe only)", "cbp": "CBP\n(adaptive)"}

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.5))
    for ax, wl in zip(axes, workloads):
        if wl not in grouped:
            ax.set_title(f"{pretty.get(wl, wl)} (no data)")
            continue
        rows = grouped[wl]
        xs = []
        ys = []
        es = []
        for v in variants:
            if v in rows:
                xs.append(pretty_var[v])
                ys.append(rows[v]["mean"] / 1000)
                es.append(rows[v]["stdev"] / 1000)
        if not xs:
            continue
        bars = ax.bar(range(len(xs)), ys, yerr=es, color=["C2", "C0", "C3", "C1"][:len(xs)])
        ax.set_xticks(range(len(xs)))
        ax.set_xticklabels(xs, rotation=15, ha="right", fontsize=9)
        ax.set_ylabel("Throughput (k TX/s)")
        ax.set_title(pretty.get(wl, wl))
        ax.grid(alpha=0.3, axis="y")
    plt.tight_layout()
    fig.savefig(OUT_DIR / "fig_v3_compare.pdf", bbox_inches="tight")
    fig.savefig(OUT_DIR / "fig_v3_compare.png", dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {OUT_DIR/'fig_v3_compare.pdf'}")


def fig_v3_v4_progression(v3, v4):
    """Round-1 → Round-2: side-by-side bars per workload showing CBP-v3 vs v4 vs default."""
    workloads = ["tpcc", "rndread", "ycsb_skewed"]
    pretty = {"tpcc": "TPC-C", "rndread": "Random Read", "ycsb_skewed": "Skewed YCSB"}
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.5))
    for ax, wl in zip(axes, workloads):
        if wl not in v4 and wl not in v3:
            continue
        # Anchor: default from v4 (most replicates).
        d = (v4.get(wl, {}).get("default", v3.get(wl, {}).get("default")) or {"mean": 0, "stdev": 0})
        cbp3 = v3.get(wl, {}).get("cbp", None)
        cbp4 = v4.get(wl, {}).get("cbp_v4", None)
        labels, means, errs, colors = [], [], [], []
        labels.append("Default\n(1/32)"); means.append(d["mean"] / 1000); errs.append(d["stdev"] / 1000); colors.append("C0")
        if cbp3:
            labels.append("CBP-v3\n(round 1)"); means.append(cbp3["mean"] / 1000); errs.append(cbp3["stdev"] / 1000); colors.append("C3")
        if cbp4:
            labels.append("CBP-v4\n(round 2)"); means.append(cbp4["mean"] / 1000); errs.append(cbp4["stdev"] / 1000); colors.append("C2")
        ax.bar(range(len(labels)), means, yerr=errs, color=colors, capsize=5)
        ax.set_xticks(range(len(labels)))
        ax.set_xticklabels(labels, fontsize=9)
        ax.set_ylabel("Throughput (k TX/s)")
        ax.set_title(pretty.get(wl, wl))
        ax.grid(alpha=0.3, axis="y")
    plt.tight_layout()
    fig.savefig(OUT_DIR / "fig_v3_v4_progression.pdf", bbox_inches="tight")
    fig.savefig(OUT_DIR / "fig_v3_v4_progression.png", dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {OUT_DIR/'fig_v3_v4_progression.pdf'}")


def main():
    main_grouped = collect(RESULTS) if RESULTS.exists() else {}
    if main_grouped:
        fig_sensitivity(main_grouped)
    v3_grouped = collect(SCALING) if SCALING.exists() else {}
    if v3_grouped:
        fig_v3_compare(v3_grouped)
    v4_grouped = collect(V4_DIR) if V4_DIR.exists() else {}
    if v3_grouped or v4_grouped:
        fig_v3_v4_progression(v3_grouped, v4_grouped)
    out = {
        "main_v2_sensitivity": main_grouped,
        "cbp_v3_compare": v3_grouped,
        "cbp_v4_compare": v4_grouped,
    }
    with open(OUT_DIR / "summary.json", "w") as f:
        json.dump(out, f, indent=2)
    print(f"  wrote {OUT_DIR/'summary.json'}")


if __name__ == "__main__":
    main()
