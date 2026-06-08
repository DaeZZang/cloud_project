#!/bin/bash
# TPB-v5: 3-workload consistency check.
# Winning config from v4: TPB_INIT_LC=5 TPB_PHASE_TOL=15 TPB_PROBE_EVERY=16.
# Compare against default 1/32 on all three workloads, n=5 each.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/tpb_v5_3wl
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-30}
THREADS=${THREADS:-16}
REPS=${REPS:-5}

declare -A WL_NAME WL_DATA WL_POOL WL_EXTRA
WL_NAME[tpcc]=tpc-c          ; WL_DATA[tpcc]=8          ; WL_POOL[tpcc]=16 ; WL_EXTRA[tpcc]=""
WL_NAME[rndread]=rndread     ; WL_DATA[rndread]=10000000; WL_POOL[rndread]=8 ; WL_EXTRA[rndread]=""
WL_NAME[ycsb_skewed]=ycsb    ; WL_DATA[ycsb_skewed]=10000000 ; WL_POOL[ycsb_skewed]=8 ; WL_EXTRA[ycsb_skewed]="ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100"

run() {
  local tag=$1; shift
  for r in $(seq 1 $REPS); do
    out="$OUT/${tag}_r${r}.csv"
    [[ -s "$out" ]] && continue
    echo "[$(date +%T)] $tag r$r :: $*"
    env -i \
      LD_LIBRARY_PATH=$LD PATH=/usr/bin:/bin \
      BLOCK="$BLOCK" BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
      "$@" \
      timeout 120 "$BIN" \
        > "$out" 2>&1 || echo "[!] $tag r$r exited non-zero"
    sleep 0.5
  done
}

for wl in tpcc rndread ycsb_skewed; do
  base="DATASIZE=${WL_DATA[$wl]} WORKLOAD=${WL_NAME[$wl]} ${WL_EXTRA[$wl]} POOLGB=${WL_POOL[$wl]}"
  run "${wl}_default" $base PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
  run "${wl}_tpb"     $base PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
       TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=15 TPB_PROBE_EVERY=16 \
       CBP_DUMP=$OUT/${wl}_tpb_dump.csv
done

echo "=== ALL DONE ==="
ls -la "$OUT"
