#!/bin/bash
# Threading-scaling experiment: PrediCache (default 1/32) vs PrediCache+CBP across 1, 4, 16, 32, 48 threads.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/scaling
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-25}
THREADS_LIST=(1 4 16 32 48)

run() {
  local tag=$1; shift
  echo "[$(date +%T)] $tag :: $*"
  env -i \
    LD_LIBRARY_PATH=$LD PATH=/usr/bin:/bin \
    BLOCK="$BLOCK" BATCH=64 RUNFOR=$RUNFOR \
    "$@" \
    timeout $((RUNFOR + 90)) "$BIN" \
      > "$OUT/${tag}.csv" 2>&1 || echo "[!] $tag exited non-zero"
  sleep 1
}

# Random-read in-memory: clean signal for fast-path benefit. 10 M entries × 128 B ≈ 1.5 GB → fits in 8 GB pool.
for t in "${THREADS_LIST[@]}"; do
  run "rndread_default_t${t}" THREADS=$t POOLGB=8 DATASIZE=10000000 WORKLOAD=rndread \
      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
  run "rndread_cbp_t${t}"     THREADS=$t POOLGB=8 DATASIZE=10000000 WORKLOAD=rndread \
      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 CBP=1
done

# Skewed YCSB: hot-data benefit
for t in "${THREADS_LIST[@]}"; do
  run "ycsb_default_t${t}"    THREADS=$t POOLGB=8 DATASIZE=10000000 WORKLOAD=ycsb \
      ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100 \
      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
  run "ycsb_cbp_t${t}"        THREADS=$t POOLGB=8 DATASIZE=10000000 WORKLOAD=ycsb \
      ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100 \
      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 CBP=1
done

# TPC-C: full mixed workload
for t in "${THREADS_LIST[@]}"; do
  run "tpcc_default_t${t}"    THREADS=$t POOLGB=16 DATASIZE=8 WORKLOAD=tpc-c \
      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
  run "tpcc_cbp_t${t}"        THREADS=$t POOLGB=16 DATASIZE=8 WORKLOAD=tpc-c \
      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 CBP=1
done

echo "=== ALL DONE ==="
ls -la "$OUT"
