#!/bin/bash
# TPB-v3: phase-tolerance fix.  Four configurations on TPC-C, n=5 each, 30 s runs:
#   default            — paper's 1/32 (baseline)
#   tpb_fair_t05       — TPB init=5, phase_tol=5  (old strict)
#   tpb_fair_t15       — TPB init=5, phase_tol=15 (new default)
#   tpb_warm_t15       — TPB init=4, phase_tol=15 (warm-start, fair veto)
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/tpb_v3
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-30}
THREADS=${THREADS:-16}
REPS=${REPS:-5}
POOLGB=16

run() {
  local tag=$1; shift
  for r in $(seq 1 $REPS); do
    out="$OUT/${tag}_r${r}.csv"
    [[ -s "$out" ]] && continue
    echo "[$(date +%T)] $tag r$r :: $*"
    env -i \
      LD_LIBRARY_PATH=$LD PATH=/usr/bin:/bin \
      BLOCK="$BLOCK" POOLGB=$POOLGB BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
      DATASIZE=8 WORKLOAD=tpc-c \
      "$@" \
      timeout 120 "$BIN" \
        > "$out" 2>&1 || echo "[!] $tag r$r exited non-zero"
    sleep 0.5
  done
}

run tpcc_default      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
run tpcc_tpb_fair_t05 PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=5  CBP_DUMP=$OUT/tpcc_tpb_fair_t05_dump.csv
run tpcc_tpb_fair_t15 PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=15 CBP_DUMP=$OUT/tpcc_tpb_fair_t15_dump.csv
run tpcc_tpb_warm_t15 PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=4 TPB_PHASE_TOL=15 CBP_DUMP=$OUT/tpcc_tpb_warm_t15_dump.csv

echo "=== ALL DONE ==="
ls -la "$OUT"
