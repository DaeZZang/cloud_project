#!/bin/bash
# Pilot: promotion-probability sensitivity sweep.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib

OUT=$ROOT/results/pilot_sweep
mkdir -p "$OUT"
RUNFOR=${RUNFOR:-20}
THREADS=${THREADS:-16}
POOLGB=${POOLGB:-8}

run_one() {
  local tag=$1
  local workload=$2
  local datasize=$3
  local chance=$4
  local extra_env=$5
  echo "[$(date +%T)] $tag (chance=$chance, workload=$workload, datasize=$datasize)"
  env -i \
    LD_LIBRARY_PATH=$LD \
    PATH=/usr/bin:/bin \
    BLOCK="$BLOCK" \
    POOLGB=$POOLGB BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
    DATASIZE=$datasize WORKLOAD=$workload \
    PROMO_OPT=$chance PROMO_SHARED=$chance PROMO_EXCL=$chance PROMO_DISPLACE=16 \
    $extra_env \
    timeout $((RUNFOR + 30)) "$BIN" \
      > "$OUT/${tag}.csv" 2>&1 || echo "[!] $tag exited non-zero"
  sleep 1
}

CHANCES=(0 4 16 32 128 1024)

for chance in "${CHANCES[@]}"; do
  run_one "tpcc_chance${chance}"        "tpc-c"          20         "$chance" ""
done
for chance in "${CHANCES[@]}"; do
  run_one "rndread_chance${chance}"     "random-read"    10000000   "$chance" ""
done
for chance in "${CHANCES[@]}"; do
  run_one "ycsb_skewed_chance${chance}" "ycsb"           10000000   "$chance" "ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100"
done

echo "Done. Logs in $OUT"
