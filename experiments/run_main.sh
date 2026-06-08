#!/bin/bash
# Main experiment: PrediCache vs PrediCache+CBP across workloads × promotion chances.
# Outputs CSVs under results/main/{workload}_{config}.csv with PrediCache's per-second log.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/main
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-30}
THREADS=${THREADS:-16}

run() {
  local tag=$1; local poolgb=$2; shift 2
  echo "[$(date +%T)] $tag (POOL=${poolgb}GB) :: $*"
  env -i \
    LD_LIBRARY_PATH=$LD PATH=/usr/bin:/bin \
    BLOCK="$BLOCK" POOLGB=$poolgb BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
    "$@" \
    timeout $((RUNFOR + 60)) "$BIN" \
      > "$OUT/${tag}.csv" 2>&1 || echo "[!] $tag exited non-zero"
  sleep 1
}

# 3 workloads × 6 fixed chances × CBP=1 = 21 runs total
WORKLOADS=("tpcc" "rndread" "ycsb_skewed")

# Sized so the working set fits in POOLGB=8 throughout RUNFOR=30 (TPC-C grows the dataset).
# 8 warehouses ≈ 0.8 GB initial; growth at ~0.3 GB/s × 30 s ≈ 9 GB total.
# We use POOLGB=16 for TPC-C to stay in-memory for the entire run.
declare -A WL_NAME WL_DATASIZE WL_EXTRA WL_POOL
WL_NAME[tpcc]=tpc-c          ; WL_DATASIZE[tpcc]=8          ; WL_EXTRA[tpcc]=""                                              ; WL_POOL[tpcc]=16
WL_NAME[rndread]=rndread     ; WL_DATASIZE[rndread]=10000000; WL_EXTRA[rndread]=""                                            ; WL_POOL[rndread]=8
WL_NAME[ycsb_skewed]=ycsb    ; WL_DATASIZE[ycsb_skewed]=10000000 ; WL_EXTRA[ycsb_skewed]="ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100" ; WL_POOL[ycsb_skewed]=8

CHANCES=(0 4 16 32 128 1024)

for wl in "${WORKLOADS[@]}"; do
  base="DATASIZE=${WL_DATASIZE[$wl]} WORKLOAD=${WL_NAME[$wl]} ${WL_EXTRA[$wl]}"
  pool=${WL_POOL[$wl]}
  for chance in "${CHANCES[@]}"; do
    run "${wl}_chance${chance}" $pool $base PROMO_OPT=$chance PROMO_SHARED=$chance PROMO_EXCL=$chance PROMO_DISPLACE=16
  done
  run "${wl}_cbp" $pool $base PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 CBP=1
done

echo "=== ALL DONE ==="
ls -la "$OUT"
