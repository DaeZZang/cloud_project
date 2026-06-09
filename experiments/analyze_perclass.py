#!/usr/bin/env python3
# Analyze A1 per-class sweep: steady-state mean throughput per config, vs global best.
import glob, os, re, statistics as st

OUT = "/lustre/s_zun212/TPC-C/results/perclass_a1"
WARMUP = 10  # drop first 10s (dataset still growing / cache filling)

def steady(path):
    vals = []
    for ln in open(path):
        p = ln.strip().split(",")
        if len(p) < 2: continue
        try: sec, tx = int(p[0]), float(p[1])
        except ValueError: continue
        if sec >= WARMUP: vals.append(tx)
    return st.mean(vals) if vals else None

cfgs = {}
for f in sorted(glob.glob(f"{OUT}/*_r*.csv")):
    tag = re.sub(r"_r\d+\.csv$", "", os.path.basename(f))
    m = steady(f)
    if m is not None: cfgs.setdefault(tag, []).append(m)

order = ["global_def32","global_best16","pc_i16_l16","pc_i8_l32","pc_i8_l128",
         "pc_i8_lnone","pc_i16_l128","pc_i16_lnone","pc_i4_l128","pc_i1_lnone"]
labels = {
 "global_def32":"GLOBAL 1/32 (paper default)",
 "global_best16":"GLOBAL 1/16 (sweep best)  <-- bar to beat",
 "pc_i16_l16":"per-class inner16 leaf16 (sanity = global16)",
 "pc_i8_l32":"per-class inner 1/8  leaf 1/32",
 "pc_i8_l128":"per-class inner 1/8  leaf 1/128",
 "pc_i8_lnone":"per-class inner 1/8  leaf never",
 "pc_i16_l128":"per-class inner 1/16 leaf 1/128",
 "pc_i16_lnone":"per-class inner 1/16 leaf never",
 "pc_i4_l128":"per-class inner 1/4  leaf 1/128",
 "pc_i1_lnone":"per-class inner always leaf never",
}
ref = st.mean(cfgs["global_best16"]) if cfgs.get("global_best16") else None
print(f"{'config':<46}{'mean k/s':>10}{'sd':>8}{'n':>4}{'Δ vs g1/16':>12}")
print("-"*84)
for tag in order:
    if tag not in cfgs:
        print(f"{labels.get(tag,tag):<46}{'(pending)':>10}"); continue
    v = cfgs[tag]; m = st.mean(v); sd = st.pstdev(v) if len(v)>1 else 0
    d = f"{100*(m-ref)/ref:+.2f}%" if ref else "—"
    print(f"{labels.get(tag,tag):<46}{m/1000:>10.1f}{sd/1000:>8.1f}{len(v):>4}{d:>12}")
