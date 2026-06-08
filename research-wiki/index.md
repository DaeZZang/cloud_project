# Research Wiki Index

**Project:** Reproduce PrediCache (SIGMOD 2026) and develop adaptive promotion (CBP)
**Last updated:** 2026-05-11

## Papers (4)

| Node | Title | Year | Relevance |
|---|---|---|---|
| paper:zinsmeister2026_predictive_translation | Predictive Translation: High-Performance Buffer Management Without the Trade-Offs | 2026 | **core** |
| paper:leis2023_vmcache | Virtual-Memory Assisted Buffer Management | 2023 | related |
| paper:leis2018_leanstore | LeanStore: In-Memory Data Management beyond Main Memory | 2018 | related |
| paper:anonymous2025_adaptiveclimb | Adaptive Cache Replacement with Dynamic Resizing | 2025 | peripheral (novelty-check) |

## Ideas (1)

| Node | Title | Stage | Outcome |
|---|---|---|---|
| idea:CBP | Cost-Benefit Adaptive Promotion | tested | partial |

## Experiments (3)

| Node | Title | Status |
|---|---|---|
| exp:E1 | Promotion-probability sensitivity sweep | completed |
| exp:E2 | CBP-v3 comparison (round 1) | completed |
| exp:E3 | CBP-v4 comparison (round 2) | completed |

## Claims (4)

| Node | Statement | Status |
|---|---|---|
| claim:C1 | 1/16 beats 1/32 on TPC-C and rndread | partial (suggestive) |
| claim:C2 | Per-class atomic counters cost 8–23 % at 16 t | **supported** |
| claim:C3 | CBP-v3 beats default | **invalidated** |
| claim:C4 | CBP-v4 matches default within 1σ on 2/3 workloads | **supported** |

## Gaps (7, 6 unresolved)

See `gap_map.md` — G1 partial (CBP), G2–G7 unresolved.
