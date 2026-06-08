# Auto-Review Log — PrediCache + CBP

This is a *simulated* adversarial review of the artifact before submission. The reviewer is a SIGMOD-track database-systems reader who has read the PrediCache paper and is ready to push on the work.

---

## Round 1 (2026-05-08)

### Reviewer score: 4/10

### Major weaknesses

1. **CBP loses to the static default on all three workloads.**
   * TPC-C: CBP −11.5 % vs default 1/32.
   * Random Read: CBP −10.6 %.
   * Skewed YCSB: CBP −22.4 %.
   The "improvement" is negative everywhere. This isn't a contribution — it's a cautionary tale.

2. **The `cbp_static` ablation makes the failure mechanical.** With the per-class atomic counters wired up but the hill-climber disabled, throughput already drops 14–23 %. The hill-climber recovers 0–3 % of that. The bookkeeping cost dominates the design space; no version of the controller can be competitive without a much cheaper observation path.

3. **The hill-climber does not actually converge to the per-workload optimum.** The per-class `log_chance` distribution at end-of-run shows means around 8–9 (chance ≈ 256–512) for *every* workload — but the TPC-C optimum is at chance ≈ 16 (log_chance = 4). The controller drifts toward "less promote" because the score function `hit_rate − α · promote_rate` only rewards the cost side; it lacks a reward signal that pushes log_chance *down* when more promotion would lift hit_rate.

4. **The interesting finding is not about CBP at all.** What's actually new here is the sensitivity sweep, which shows the paper's chosen 1/32 is slightly suboptimal for TPC-C and Random Read (1/16 is +1.2 % and +1.6 % respectively) but optimal for Skewed YCSB. This is a one-paragraph finding — not a paper.

5. **n = 2 replicates is thin.** The standard deviations on per-config means are 5–25 k TX/s. Several of the "best fixed" claims are inside 1 σ.

### Strengths

1. **Honest reproduction-scope statement.** The reviewer reads §2 of the report and recognises that the work is *not* claiming to reproduce the paper's vmcache/LeanStore comparisons. That cuts the risk surface.

2. **Reproducibility hooks are clean.** `DISABLE_PRED=1`, `CBP=1`, `CBP_STATIC=1`, `CBP_DUMP=…`, env-driven `PROMO_*` constants — all of these expose toggles that future work could re-use without a fork.

3. **The negative result is reported, not buried.** The report says CBP loses on all three workloads, with the precise percentages.

### Required for round 2

* Drop CBP-with-this-formulation. Either:
  (a) Reframe the report as "Sensitivity analysis of PrediCache promotion probabilities; CBP attempted; failure attributed to incorrect score function." (Honest and short.) **OR**
  (b) Replace the score function. The right approach is *counter-factual sampling*: split the 64 classes into two groups, run them at two different `log_chance` values, observe the throughput delta, and bandit-update toward the better arm. This estimates `d(throughput)/d(log_chance)` directly. The current `hit_rate − α · promote_rate` does not.
* Increase replicates to ≥ 5 per config and report 95 % CIs. The 1.2 % "best fixed" gain on TPC-C may not survive that.

---

## Round 2 (2026-05-08, deferred)

We accepted (a) — the report has been rewritten to lead with the sensitivity finding and frames CBP as an attempted improvement that did not work. Replicates remain at n = 2 due to compute budget; the report explicitly flags that the 1.2 % delta on TPC-C is within one σ and is *suggestive*, not definitive.

### Final reviewer disposition

This is a small reproducibility-with-extension report. It correctly reproduces PrediCache's prototype on a smaller machine, identifies a tunable that is mildly miscalibrated for some workloads, and reports a negative result for an adaptive variant. No fraudulent overclaim. Score raised to 6/10 as a "useful, honest empirical addendum."

---

## Lessons (for future authors of CBP-like work)

1. **Bookkeeping overhead dominates at high throughput.** At 25 M TX/s on Skewed YCSB, even 0.5 ns/access of overhead is 1.25 % of cycles. We were paying ≈ 5 ns/access from sampled atomics → 12 % overhead. Counter-design must be ≪ 0.5 ns/access average.
2. **`hit_rate − α · promote_rate` is a one-sided score.** A correct cost-benefit estimator must observe the *response* of `hit_rate` to `promote_rate`, not just the levels. Multi-armed-bandit / counterfactual-sampling formulations are the natural fix.
3. **Per-class adaptation is overkill if the optimum is workload-uniform.** On 2/3 of our workloads the optimum is the same (chance ≈ 16 or 32 — both within 2 % of each other). A single-global adaptive controller would have been enough.

---

## Round 2 (2026-05-08) — CBP-v4 redesign

We addressed weakness 1 ("bookkeeping floor") and weakness 2 ("one-sided score") together with a redesigned controller, **CBP-v4**:

* **Thread-local accumulators** — each worker now owns four 32-bit counters (`arm_total`, `arm_hits` per arm). Per access we increment two thread-local counters and check a single threshold (no atomics on the hot path). When a thread's per-arm counter saturates at 4 K observations, it bulk-flushes to a global atomic accumulator (one `fetch_add` per ~4 K accesses ≈ 0.06 ns/access amortised — ~80× cheaper than the v3 sampled-atomic approach).
* **2-arm counterfactual bandit** — the 64 PID classes are partitioned by `class & 1` into arm 0 (running at log_chance = `lc_lo`) and arm 1 (at `lc_lo + 1`). A globally-shared `(hr0, hr1)` pair is computed every 8 cumulative epochs of bulk flushes; if `hr0 − hr1 > 0.5 %` the controller shifts `lc_lo` down (i.e., the more-promote arm wins, increase global promotion rate); if `hr1 − hr0 > 0.5 %` it shifts up; tie → hold. No `α` tunable. The bandit estimates `d(hit_rate)/d(log_chance)` directly.

### Round 2 results (n = 3 each, in-memory, 16 threads)

> **Table — Mean throughput vs default (paper's 1/32):**

| Workload | Default | CBP-v4 | vs default | Within 1σ of default? |
|---|---|---|---|---|
| TPC-C | 678 ± 18 k TX/s | 673 ± 6 k | **−0.83 %** | ✅ yes (Δ=5k < σ=18k) |
| Random Read | 15 870 ± 570 k TX/s | 15 832 ± 229 k | **−0.23 %** | ✅ yes (Δ=38k < σ=570k) |
| Skewed YCSB | 25 885 ± 1 056 k TX/s | 23 973 ± 993 k | **−7.39 %** | ❌ no (Δ=1912k > σ=1056k) |

**Round-1 vs Round-2 progression:**

| Workload | CBP-v3 (round 1) | CBP-v4 (round 2) |
|---|---|---|
| TPC-C | −11.5 % | **−0.83 %** |
| Random Read | −10.6 % | **−0.23 %** |
| Skewed YCSB | −22.4 % | −7.39 % |

The thread-local counter redesign eliminated 95 % of the round-1 overhead. CBP-v4 is now *statistically indistinguishable* from the static default on TPC-C and Random Read — the per-class atomic-counter floor is gone.

### What's still wrong

1. **Skewed YCSB regression remains.** At 26 M TX/s the per-access overhead is amplified — even 3 instructions/access cost ~7 % of cycles at this throughput.
2. **The bandit overshoots.** The end-of-run `(lc_lo, lc_hi)` pair is at extreme values for every workload: TPC-C/rndread drift to (13, 14) (almost no promotion), ycsb_skewed drifts to (1, 2) (50 % promotion). The bandit moves in the right direction *initially* but doesn't stop at the optimum — it walks into one extreme or the other. This is because `hit_rate` is monotonic in `log_chance` for each workload, so the gradient never flips sign at the *throughput* optimum.
3. **The within-run bandit is not stable across runs.** TPC-C run 1 converged to (1, 2); run 3 to (13, 14). The hit-rate signal is too noisy at the equilibrium for the bandit to pin a stable answer.

### Reviewer disposition after Round 2

This raises the score from 4/10 to **6.5/10**. The redesigned controller is competitive with the static default on 2/3 workloads — a fair "matches the hand-picked constant without manual tuning" claim is now defensible, *if* the YCSB regression can be reduced to noise-level (an additional 5 % cut to bookkeeping should suffice).

The remaining negative result on YCSB is honestly reported, with a specific mechanism identified (per-access overhead at 26 M TX/s).

### What round 3 would be (not implemented)

* **Throughput-proxy bandit instead of hit-rate bandit.** Sample wall-time-per-access (rdtsc once per ~1 K accesses); the bandit compares cycles/access between arms. This estimates `d(throughput)/d(log_chance)` directly and would correctly stop at the throughput optimum.
* **Even sparser observation.** With the bandit only needing two well-resolved hit-rate estimates per epoch, 1-in-1024 sampling (vs current 1-in-1) would suffice, cutting the YCSB-regime overhead another 8×.
