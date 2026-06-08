#!/bin/bash
# CBP-v4 — bandit + thread-local counters. 3 reps per cell.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/cbp_v4
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-25}
THREADS=${THREADS:-16}
REPS=${REPS:-3}

run() {
  local tag=$1; local poolgb=$2; shift 2
  for r in $(seq 1 $REPS); do
    out="$OUT/${tag}_r${r}.csv"
    [[ -s "$out" ]] && continue
    echo "[$(date +%T)] $tag r$r (POOL=${poolgb}GB) :: $*"
    env -i \
      LD_LIBRARY_PATH=$LD PATH=/usr/bin:/bin \
      BLOCK="$BLOCK" POOLGB=$poolgb BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
      "$@" \
      timeout $((RUNFOR + 60)) "$BIN" \
        > "$out" 2>&1 || echo "[!] $tag r$r exited non-zero"
    sleep 0.5
  done
}

declare -A WL_NAME WL_DATASIZE WL_EXTRA WL_POOL
WL_NAME[tpcc]=tpc-c          ; WL_DATASIZE[tpcc]=8          ; WL_EXTRA[tpcc]=""                                              ; WL_POOL[tpcc]=16
WL_NAME[rndread]=rndread     ; WL_DATASIZE[rndread]=10000000; WL_EXTRA[rndread]=""                                            ; WL_POOL[rndread]=8
WL_NAME[ycsb_skewed]=ycsb    ; WL_DATASIZE[ycsb_skewed]=10000000 ; WL_EXTRA[ycsb_skewed]="ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100" ; WL_POOL[ycsb_skewed]=8

for wl in tpcc rndread ycsb_skewed; do
  base="DATASIZE=${WL_DATASIZE[$wl]} WORKLOAD=${WL_NAME[$wl]} ${WL_EXTRA[$wl]}"
  pool=${WL_POOL[$wl]}

  run "${wl}_default" $pool $base PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16
  run "${wl}_cbp_v4"  $pool $base PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
      CBP_V4=1 CBP_DUMP=$OUT/${wl}_cbp_v4_dump.csv
done

echo "=== ALL DONE ==="
ls -la "$OUT"
