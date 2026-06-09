#!/bin/bash
# A1 — per-class (inner vs leaf) static promotion headroom test, TPC-C.
# Question: can giving B-tree inner nodes (hot) and leaf nodes (cold) DIFFERENT
# promotion probabilities beat the single global best (1/16) on TPC-C?
# Diagnostic (10s,n=1): inner-always/leaf-never=695k vs inner-never/leaf-always=553k
# => classification works and the role asymmetry is large. This is the rigorous run.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
ENV=/home/s_zun212/miniconda3/envs/flygcl
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
OUT=$ROOT/results/perclass_a1
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-30}
THREADS=${THREADS:-16}
REPS=${REPS:-5}
BASE="DATASIZE=8 WORKLOAD=tpc-c POOLGB=16 PROMO_DISPLACE=16"

run() {
  local tag=$1; shift
  for r in $(seq 1 $REPS); do
    out="$OUT/${tag}_r${r}.csv"
    [[ -s "$out" ]] && continue
    echo "[$(date +%T)] $tag r$r :: $*"
    env -i LD_LIBRARY_PATH=$ENV/lib PATH=/usr/bin:/bin \
      BLOCK="$BLOCK" BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
      $BASE "$@" \
      timeout 120 "$BIN" > "$out" 2>&1 || echo "[!] $tag r$r exited non-zero"
    sleep 0.5
  done
}

# ── Global controls (PERCLASS off) — the bars to beat ──
run global_def32   PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32   # paper default
run global_best16  PROMO_OPT=16 PROMO_SHARED=16 PROMO_EXCL=16   # single-constant best (sweep optimum)

# ── Per-class sanity: inner=leaf should reproduce the global path ──
run pc_i16_l16  PERCLASS=1 PROMO_INNER=16 PROMO_LEAF=16

# ── Per-class hypothesis grid: promote inner (hot) aggressively, leaf (cold) less ──
run pc_i8_l32    PERCLASS=1 PROMO_INNER=8  PROMO_LEAF=32
run pc_i8_l128   PERCLASS=1 PROMO_INNER=8  PROMO_LEAF=128
run pc_i8_lnone  PERCLASS=1 PROMO_INNER=8  PROMO_LEAF=16384
run pc_i16_l128  PERCLASS=1 PROMO_INNER=16 PROMO_LEAF=128
run pc_i16_lnone PERCLASS=1 PROMO_INNER=16 PROMO_LEAF=16384
run pc_i4_l128   PERCLASS=1 PROMO_INNER=4  PROMO_LEAF=128
run pc_i1_lnone  PERCLASS=1 PROMO_INNER=1  PROMO_LEAF=16384   # inner always, leaf never (extreme)

echo "=== ALL DONE ==="
ls -la "$OUT"
