#!/bin/bash
# Microarchitectural analysis with `perf stat`. Single-threaded random read.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/perf
mkdir -p "$OUT"

PERF_EVENTS="cycles,instructions,L1-dcache-load-misses,LLC-load-misses,branch-misses,dTLB-load-misses"

run_perf() {
  local tag=$1; shift
  echo "[$(date +%T)] $tag :: $*"
  ( env -i \
      LD_LIBRARY_PATH=$LD PATH=/usr/bin:/bin \
      BLOCK="$BLOCK" BATCH=64 RUNFOR=20 THREADS=1 \
      DATASIZE=10000000 WORKLOAD=random-read POOLGB=8 \
      "$@" \
      perf stat -e "$PERF_EVENTS" timeout 60 "$BIN" \
  ) > "$OUT/${tag}.log" 2>&1 || echo "[!] $tag returned non-zero"
}

run_perf default PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
run_perf cbp     PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 CBP=1

echo "DONE"
