# Auto-Review Prompt for CBP / PrediCache Method Development

You are reviewing a research artifact that:

1. **Reproduces** PrediCache (Zinsmeister et al., SIGMOD 2026) — a buffer-manager design that uses *predictive translation* (deterministic preferred frame per PID + speculative read on the predicted frame) and *probabilistic promotion* (1/32 / 1/512 hand-picked constants) to keep hot pages in their preferred frames.
2. **Develops** Cost-Benefit Adaptive Promotion (CBP) — a lock-free, 4 KB-of-state per-class hill-climber that adapts the promotion probability online based on observed `score = hit_rate − α · promote_rate`.

The reproduction is constrained: no NVMe (Lustre back-end), no vmcache (no kernel-module access), 52 cores (vs paper's 192). We test only the *internal* claim (CBP vs. paper's fixed 1/32) on TPC-C / random-read / skewed YCSB.

## Specific things to push hard on

1. **Premise validity.** Does the throughput-vs-fixed-chance curve genuinely demonstrate workload-dependent optima (the premise of CBP)? Sub-1% deltas with 20-40k stdev across replicates are NOT evidence — flag this if true.
2. **CBP overhead.** Per-access atomic increments + sampling — what's the overhead in cycles? Did we measure it directly? Without that, we can't claim "negligible" overhead.
3. **Adaptation correctness.** Does the per-class log_chance distribution (dumped via CBP_DUMP) actually differ between workloads? A degenerate distribution (all 14 or all 1) means the hill climber is bouncing on noise, not converging.
4. **Statistical strength.** 2 reps per config is thin. Variance is large. The paper reports much more stable numbers — is our setup reproducing single-machine variance or are we masking real differences in noise?
5. **Reproduction faithfulness.** Without vmcache/LeanStore comparisons, can we show PrediCache > Traditional internal baseline? If we don't run the Traditional ablation we cannot claim reproduction.
6. **Α sensitivity.** CBP's `α=0.20` is itself a tuned constant. Is CBP just hyper-parameter shuffling — replacing `1/32` with `α=0.20` — without a real architectural improvement?

## Things to NOT push on

- Comparison against vmcache / LeanStore / WiredTiger / LMDB — we have honestly stated the constraint.
- Absolute throughput vs paper — different hardware.
- OOM regime — Lustre isn't NVMe.

## Decision criteria

CBP is interesting if:
- (A) The fixed-chance sweep shows a workload-dependent optimum that differs by ≥5 % across workloads, AND
- (B) CBP is within 2 % of the per-workload best fixed and ≥3 % above the worst fixed across the 3 workloads, AND
- (C) The per-class log_chance distribution is workload-distinct (visibly different histograms across workloads).

If (A) is false → no story; CBP is overengineering.
If (B) is false → adaptation isn't worth its cost.
If (C) is false → CBP is doing something else (random walk).

We will report the honest verdict either way.

## Artifacts

- `idea-stage/IDEA_REPORT.md` — idea generation + novelty
- `repo/PrediCache/cbp.hpp` — implementation
- `repo/PrediCache/buffer_manager.hpp`, `guards.hpp` — wiring
- `experiments/run_main_v2.sh` — main sweep
- `results/main_v2/*.csv` — per-config CSV (per-second throughput)
- `results/main_v2/*_cbp_dump.csv` — per-class log_chance at end of run
- `NARRATIVE_REPORT.md` — current narrative (TBD-marked)

Please read these in this order: NARRATIVE_REPORT.md → cbp.hpp → recent results in results/main_v2 → IDEA_REPORT.md.
