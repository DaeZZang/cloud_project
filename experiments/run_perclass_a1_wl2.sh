#!/bin/bash
# A1 per-class (inner vs leaf) static promotion headroom test — rndread + skewed YCSB.
# Same grid as the TPC-C run (run_perclass_a1.sh). Each workload is a single
# B-tree, so leaf nodes dominate even more than in TPC-C.
set -euo pipefail

ROOT=/lustre/s_zun212/TPC-C
ENV=/home/s_zun212/miniconda3/envs/flygcl
BIN=$ROOT/repo/PrediCache/predicache
BLOCK=$ROOT/predi_test.bm
OUT=$ROOT/results/perclass_a1_wl2
mkdir -p "$OUT"

RUNFOR=${RUNFOR:-30}
THREADS=${THREADS:-16}
REPS=${REPS:-5}

declare -A WL_BASE
WL_BASE[rndread]="DATASIZE=10000000 WORKLOAD=rndread POOLGB=8 PROMO_DISPLACE=16"
WL_BASE[ycsb]="DATASIZE=10000000 WORKLOAD=ycsb ZIPF_FACTOR=0.9 OPS_PER_TX=1 READ_RATIO=100 POOLGB=8 PROMO_DISPLACE=16"

run() {
  local tag=$1; shift
  for r in $(seq 1 $REPS); do
    out="$OUT/${tag}_r${r}.csv"
    [[ -s "$out" ]] && continue
    echo "[$(date +%T)] $tag r$r :: $*"
    env -i LD_LIBRARY_PATH=$ENV/lib PATH=/usr/bin:/bin \
      BLOCK="$BLOCK" BATCH=64 RUNFOR=$RUNFOR THREADS=$THREADS \
      "$@" \
      timeout 180 "$BIN" > "$out" 2>&1 || echo "[!] $tag r$r exited non-zero"
    sleep 0.5
  done
}

for wl in rndread ycsb; do
  B="${WL_BASE[$wl]}"
  # global controls (PERCLASS off)
  run ${wl}_global_def32   $B PROMO_OPT=32 PROMO_SHARED=32 PROMO_EXCL=32
  run ${wl}_global_best16  $B PROMO_OPT=16 PROMO_SHARED=16 PROMO_EXCL=16
  # per-class sanity (inner=leaf -> global path)
  run ${wl}_pc_i16_l16     $B PERCLASS=1 PROMO_INNER=16 PROMO_LEAF=16
  # per-class hypothesis grid (promote hot inner more, cold leaf less)
  run ${wl}_pc_i8_l32      $B PERCLASS=1 PROMO_INNER=8  PROMO_LEAF=32
  run ${wl}_pc_i8_l128     $B PERCLASS=1 PROMO_INNER=8  PROMO_LEAF=128
  run ${wl}_pc_i8_lnone    $B PERCLASS=1 PROMO_INNER=8  PROMO_LEAF=16384
  run ${wl}_pc_i16_l128    $B PERCLASS=1 PROMO_INNER=16 PROMO_LEAF=128
  run ${wl}_pc_i16_lnone   $B PERCLASS=1 PROMO_INNER=16 PROMO_LEAF=16384
  run ${wl}_pc_i4_l128     $B PERCLASS=1 PROMO_INNER=4  PROMO_LEAF=128
  run ${wl}_pc_i1_lnone    $B PERCLASS=1 PROMO_INNER=1  PROMO_LEAF=16384
done

echo "=== ALL DONE ==="
ls "$OUT" | wc -l
