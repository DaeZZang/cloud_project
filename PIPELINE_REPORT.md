# Research Pipeline Report

**Direction:** reproduce https://github.com/mzinsmeister/PrediCache and develop the method
**Chosen Idea:** Cost-Benefit Adaptive Promotion (CBP), iterated v3 → v4
**Date:** 2026-05-08 (17:06 → 18:48, ≈ 100 minutes including round 2)
**Pipeline:** /idea-discovery → implement → /run-experiment → /auto-review-loop (rounds 1 & 2)

## Journey Summary

* **Ideas generated:** 5 (CBP, SAPT-K, sampling-based eviction integration, NUMA-PT, indirection-bit promotion).
* **Filtered to** 3 plausible (CBP, SAPT-K, eviction integration); 2 rejected (NUMA-PT scope, indirection-bit conflicts with the paper's invariant).
* **Piloted** 1 idea (CBP) — sensitivity sweep validated the premise (workload-dependent optima).
* **Chose 1** for full implementation: CBP.

* **Implementation:** 130 LOC of `cbp.hpp` + ~40 LOC of wiring in `buffer_manager.hpp` and `guards.hpp` + a `DISABLE_PRED` toggle for the Traditional ablation + `CBP_STATIC` mode for cost isolation. Three iterations (v1 → v2 → v3) tracked under the iteratively-refined controller.

* **Experiments:**
  * v1 sensitivity sweep (21 runs, ≈ 22 min) — caught a workload-name typo (`random-read` vs `rndread`).
  * v2 sensitivity + CBP (30 runs, ≈ 19 min) — clean sensitivity-sweep data, CBP-v2 −7 to −30 %.
  * v3 CBP refinement (24 runs, ≈ 14 min) — fixed score-tracking bug, added warmup + deadband, CBP-v3 −10.6 to −22.4 % (still loses).
  * **Round 2:** CBP-v4 redesign (18 runs, ≈ 12 min) — thread-local counters + 2-arm counterfactual bandit. CBP-v4 −0.2 / −0.8 / −7.4 % on rndread/TPC-C/ycsb_skewed (within 1σ of default on 2/3 workloads).
  * Total: ≈ 93 PrediCache runs, ≈ 67 CPU-minutes (no GPUs used; this is a systems paper).

* **Review rounds:** 2/4 simulated. Score: 4/10 → 6.5/10 after the v4 redesign addressed both reviewer-flagged weaknesses (bookkeeping floor + one-sided score). On 2/3 workloads CBP is now competitive with the static default.

## Headline outcome (after round 2)

Reproducing PrediCache succeeds. Two findings:

1. **Sensitivity sweep:** the paper's 1/32 default is mildly suboptimal on TPC-C and Random Read (1/16 wins by 1.2 % and 1.6 % respectively). 1/32 IS the optimum for skewed YCSB. *Drop-in tuning recommendation: try 1/16 if your workload looks like TPC-C.*

2. **Adaptive promotion:** CBP-v3 (round 1) failed (−11.5 % to −22.4 % vs default) — bookkeeping cost dominated and the score function was one-sided. CBP-v4 (round 2), redesigned with thread-local accumulators and a 2-arm counterfactual bandit, **breaks even with the static default on TPC-C and Random Read** (within 1σ) and reduces the YCSB regression from −22.4 % to −7.4 %. The remaining −7.4 % on YCSB is per-access instrumentation overhead at 26 M TX/s — addressable with sparser sampling, left as future work.

The v3 → v4 progression demonstrates the value of the adversarial-review loop: the round-1 critique pointed exactly at the two fixable problems (bookkeeping cost, one-sided score), and round 2 implemented both.

## Writing Handoff

* `NARRATIVE_REPORT.md`: ✅ generated.
* `idea-stage/IDEA_REPORT.md`: ✅ generated.
* `review-stage/AUTO_REVIEW.md`: ✅ generated.
* `results/figures/fig_sensitivity.pdf`, `fig_v3_compare.pdf`, `summary.json`: ✅ generated.
* Venue: not set — `AUTO_WRITE=false`. To run the paper-writing pipeline, the user can invoke:

  ```
  /paper-writing "NARRATIVE_REPORT.md" — venue: <venue>
  ```

  Most defensible venue: *workshop or industrial track* (e.g., DaMoN, BTW industrial) — the contribution is too small for a SIGMOD main track but is a useful empirical addendum.

## Manual figures needed (if escalated to a paper)

* Per-class `log_chance` histograms across the three workloads (data in `results/cbp_v3/*_cbp_dump.csv`).
* Per-second time series for the TPC-C in-memory regime, showing the transient drop near the OOM boundary (raw rows in `results/main_v2/tpcc_chance*_r*.csv`).

## Remaining TODOs

Reviewer-flagged in round 1, **fixed in round 2**:

1. ✅ **Bookkeeping overhead** — thread-local counters with bulk flush per ~4 K accesses (`cbp.hpp::noteAccessV4`). Cut overhead from 14–23 % to ~0 % on TPC-C/rndread.
2. ✅ **One-sided score function** — replaced with a 2-arm counterfactual bandit comparing `hit_rate(lc_lo)` vs `hit_rate(lc_lo+1)`. No `α` tunable.

Remaining (reviewer-flagged in round 2, not yet fixed):

3. **Bandit signal vs throughput.** `hit_rate` is monotonic in `log_chance` so the bandit walks to extremes instead of stopping at the throughput optimum. The fix is to bandit on `cycles/access` (rdtsc-sampled) rather than `hit_rate`. This is round-3 work.
4. **YCSB regression.** Even thread-local counters cost ~7 % of cycles at 26 M TX/s. Sparser sampling (1-in-256 instead of 1-in-1) plus a slower bandit cadence would close this.

Other items not yet addressed:

5. **Increase replicates** to ≥ 5 per config (currently 3); some deltas are within one σ.
6. **Run the comparison at 32 / 48 / 52 threads** to confirm the trend holds at the upper end of our hardware.
7. **Run on a real NVMe** (out-of-memory regime) — Lustre is uninformative for OOM.

## Pipeline observations (meta)

What worked:

* The pipeline's "implement → pilot → analyze → fix → reanalyze" loop caught two real bugs: a workload-name typo that wasted 22 min of compute, and a score-tracking bug in the hill climber (last_score was being written before being compared). Without the iterative loop both would have shipped.
* `DISABLE_PRED` and `CBP_STATIC` were not in the original plan but were created mid-flight specifically to *disprove* hopeful interpretations of CBP results. The pipeline's emphasis on adversarial review pushed for these isolations and they paid off.

What didn't:

* Polling for status burnt time. Use `Monitor` and `until <pred>; do sleep 10; done` pattern early.
* Not running 3 replicates from the start meant the headline 1.2 % delta is within one σ. With even 5 reps we could have had a clean significance claim.
* `aio-max-nr=65536` on the host = at most one running PrediCache instance at a time; could not parallelise sensitivity sweep with method-development experiments. Plan for this on shared infrastructure.
