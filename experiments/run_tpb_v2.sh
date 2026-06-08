#!/bin/bash
# TPB-v2: with bug fixes (last_dir update + TPB_INIT_LC knob).
# Three configurations on TPC-C, n=5 each, 30 s runs:
#   default       — paper's 1/32, 1/512
#   tpb_fair      — TPB starting at lc=5 (chance=1/32, same as default ⇒ fair adaptation cost)
#   tpb_warm      — TPB warm-started at lc=4 (chance=1/16, sensitivity-sweep optimum)
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/tpb_v2
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
      timeout $((RUNFOR + 60)) "$BIN" \
        > "$out" 2>&1 || echo "[!] $tag r$r exited non-zero"
    sleep 0.5
  done
}

run tpcc_default  PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
run tpcc_tpb_fair PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 CBP_DUMP=$OUT/tpcc_tpb_fair_dump.csv
run tpcc_tpb_warm PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=4 CBP_DUMP=$OUT/tpcc_tpb_warm_dump.csv

echo "=== ALL DONE ==="
ls -la "$OUT"
