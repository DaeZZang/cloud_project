#!/bin/bash
# TPB-v4: Phase-tolerance grid + probe-rate ablation.
#
# Hypothesis from v3 analysis: with the bug-fixed controller, the dominant cost
# is the *probe tax* (~2 % from 3 probe sandwiches × 30 s run), not the phase
# guard.  This run varies both knobs to isolate which one matters.
#
# Configurations (5 reps each, TPC-C, 16 threads, 30 s):
#   default                  baseline (no TPB)
#   tpb_t05_p08              phase_tol=5%,   probe_every=8     (original)
#   tpb_t100_p08             phase_tol=100%, probe_every=8     (phase guard off)
#   tpb_t15_p16              phase_tol=15%,  probe_every=16    (slower probes)
#   tpb_t100_p16             phase_tol=100%, probe_every=16    (combined)
#   tpb_warm_t100_p16        same as above + init=4 (warm)
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
LD=/home/s_zun212/miniconda3/envs/flygcl/lib
OUT=$ROOT/results/tpb_v4
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

run tpcc_default          PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16

run tpcc_tpb_t05_p08      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=5   TPB_PROBE_EVERY=8 \
     CBP_DUMP=$OUT/tpcc_tpb_t05_p08_dump.csv

run tpcc_tpb_t100_p08     PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=100 TPB_PROBE_EVERY=8 \
     CBP_DUMP=$OUT/tpcc_tpb_t100_p08_dump.csv

run tpcc_tpb_t15_p16      PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=15  TPB_PROBE_EVERY=16 \
     CBP_DUMP=$OUT/tpcc_tpb_t15_p16_dump.csv

run tpcc_tpb_t100_p16     PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=100 TPB_PROBE_EVERY=16 \
     CBP_DUMP=$OUT/tpcc_tpb_t100_p16_dump.csv

run tpcc_tpb_warm_t100_p16 PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32 PROMO_DISPLACE=16 \
     TPB=1 TPB_INIT_LC=4 TPB_PHASE_TOL=100 TPB_PROBE_EVERY=16 \
     CBP_DUMP=$OUT/tpcc_tpb_warm_t100_p16_dump.csv

echo "=== ALL DONE ==="
ls -la "$OUT"
