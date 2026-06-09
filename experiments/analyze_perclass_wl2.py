#!/usr/bin/env python3
# Analyze A1 per-class sweep for rndread + ycsb. Steady-state mean, Welch t vs both bars.
import glob, os, re, statistics as st, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "/lustre/s_zun212/TPC-C/results/perclass_a1_wl2"
W = 10

def steady(p):
    v = []
    for ln in open(p):
        a = ln.strip().split(",")
        if len(a) < 2: continue
        try: s, t = int(a[0]), float(a[1])
        except ValueError: continue
        if s >= W: v.append(t)
    return st.mean(v) if v else None

def welch(a, b):
    if len(a) < 2 or len(b) < 2: return 0
    se = (st.variance(a)/len(a) + st.variance(b)/len(b))**0.5
    return (st.mean(a)-st.mean(b))/se if se else 0

C = {}
for f in glob.glob(f"{OUT}/*_r*.csv"):
    tag = re.sub(r"_r\d+\.csv$", "", os.path.basename(f))
    m = steady(f)
    if m is not None: C.setdefault(tag, []).append(m)

grid = ["global_def32","global_best16","pc_i16_l16","pc_i8_l32","pc_i8_l128",
        "pc_i8_lnone","pc_i16_l128","pc_i16_lnone","pc_i4_l128","pc_i1_lnone"]
for wl in ["rndread","ycsb"]:
    g16 = C.get(f"{wl}_global_best16"); g32 = C.get(f"{wl}_global_def32")
    if not g16 or not g32:
        print(f"\n### {wl}: (incomplete)"); continue
    m16, m32 = st.mean(g16), st.mean(g32)
    print(f"\n### {wl}  —  bar1=global 1/16: {m16/1000:.0f}k   bar2=global 1/32: {m32/1000:.0f}k")
    print(f"{'config':<16}{'mean k/s':>10}{'sd':>7}{'Δ vs1/16':>10}{'|t|1/16':>9}{'Δ vs1/32':>10}{'|t|1/32':>9}")
    for g in grid:
        tag = f"{wl}_{g}"; v = C.get(tag)
        if not v: print(f"{g:<16}{'(pending)':>10}"); continue
        m, sd = st.mean(v), (st.stdev(v) if len(v)>1 else 0)
        d16 = 100*(m-m16)/m16; d32 = 100*(m-m32)/m32
        print(f"{g:<16}{m/1000:>10.0f}{sd/1000:>7.0f}{d16:>+9.2f}%{abs(welch(v,g16)):>9.2f}{d32:>+9.2f}%{abs(welch(v,g32)):>9.2f}")
print("\n(95% significance: |t| > 2.31)")
