#!/bin/bash
# TPB (Throughput-Probed Bandit) vs default 1/32 on TPC-C. n=5.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/tpb
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

run tpcc_default PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
run tpcc_tpb     PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 TPB=1 CBP_DUMP=$OUT/tpcc_tpb_dump.csv

echo "=== ALL DONE ==="
ls -la "$OUT"
