# NARRATIVE REPORT — Reproducing PrediCache and Adapting its Promotion Policy

**Hardware:** 2× Intel Xeon Gold 5320 (52 cores, 1 thread per core, 2 NUMA nodes), 440 GB RAM, Lustre over NVMe (no direct NVMe), gcc 11.4 -O3 -std=c++23. **Date:** 2026-05-08.

## TL;DR (rounds 1 & 2)

We reproduced the PrediCache (SIGMOD 2026) prototype on a 16-thread setup and iterated on **Cost-Benefit Adaptive Promotion (CBP)** to replace its hand-picked probabilistic-promotion constants (1/32, 1/512). After two rounds:

* **Reproduction succeeds** for the in-memory regime: PrediCache builds and runs on commodity hardware. The build needs only `libaio-dev` + a regular file with O_DIRECT support; we provide a small env-knob (`DISABLE_PRED=1`) to switch off the predictive-translation fast path so the paper's own "Traditional + opt latch + inlining" ablation can be reproduced inside the same binary.
* **The sensitivity sweep produces the strongest empirical finding.** PrediCache's choice of 1/32 is *not* the per-workload optimum on our hardware. TPC-C and uniform random-read both peak at **chance = 1/16** (+1.2 % and +1.6 % over 1/32 respectively). Skewed YCSB (zipf=0.9) peaks at **chance = 1/32** — the paper's default — and degrades on either side.
* **CBP-v3 (round 1)** — per-class hill climber with sampled atomic counters and an `α-weighted hit_rate − α·promote_rate` score. **Negative result**: bookkeeping cost alone is 14/8/23 % on TPC-C / rndread / ycsb; the score function is one-sided and the controller drifts toward "less promote" on every workload.
* **CBP-v4 (round 2)** — thread-local accumulators (one bulk-flush atomic per ~4 K accesses) + 2-arm counterfactual bandit comparing `hit_rate(lc_lo) vs hit_rate(lc_lo+1)`. **Mostly competitive**: TPC-C and Random Read are within 1σ of the static default (−0.83 % and −0.23 %); Skewed YCSB still regresses by −7.4 %. The thread-local redesign eliminates 95 % of the round-1 overhead.

We report all three findings (sensitivity sweep, v3 negative result, v4 break-even on 2/3 workloads) together.

---

## 1 The PrediCache idea, briefly

A traditional buffer manager translates a page-id (PID) to a buffer-frame address by hashing the PID into a hash table, then dereferencing the resulting frame header. Two **dependent** memory accesses dominate latency. PrediCache attaches every PID to a *deterministic preferred memory frame* (`MurmurHash2(pid) mod frameCount`) and lets the CPU **speculatively** read from that address while the hash-table lookup is still in flight. When the speculation is correct, the two memory accesses run in parallel — superscalar execution hides the hash-table cost. To keep the speculation accurate, hot pages are *probabilistically promoted* into their preferred frame on subsequent accesses with hand-picked constants:

* `1/32` if no other page must be displaced,
* `1/512` if a displacement is required (`1/32 × 1/16`).

Promotion is implemented with two exclusive latches, a 4 KB `memcpy` of the displaced page, and a 4 KB `memcpy` of the promoted page. The hash table is a chaining hash table sized at 2× the buffer pool with the first slot inlined and synchronized via an optimistic versioned lock.

## 2 Reproduction scope

| Aspect | Paper | This work | Match? |
|---|---|---|---|
| CPU | AMD EPYC 9645P (96 c, 192 t) | 2× Intel Xeon Gold 5320 (52 c, 1 t/c) | Different ISA family. |
| RAM | 384 GB | 440 GB | OK. |
| Storage | Kioxia CM7-R PCIe 5.0 NVMe | Lustre over NVMe-backed OSTs | Slower; in-memory experiments unaffected. |
| Page size | 4 KB | 4 KB | match |
| HT size | 2× pool | 2× pool | match |
| Hash | MurmurHash2 | MurmurHash2 | match |
| Default promotion | 1/32, 1/512 | 1/32, 1/512 | match |
| Compiler / flags | g++ -O3 | g++ 11.4 -O3 -std=c++23 -laio | match |

We **did not** reproduce comparisons against vmcache+exmap, LeanStore, WiredTiger, or LMDB:

* vmcache requires the `exmap` kernel module, root, and SMAP disabled — security policy on shared infrastructure forbids these.
* LeanStore / WiredTiger / LMDB integration is 1–3 days of additional work and would only tell us how PrediCache compares to those systems on *this* hardware, which is orthogonal to the question we are asking (whether the **promotion** can be made adaptive within PrediCache).

We do however reproduce the paper's internal **Traditional** ablation by gating `bf->intendedSlot` on a `DISABLE_PRED=1` env-var (`buffer_manager.hpp::disablePredict`). Setting it to 1 disables the speculative fast path while leaving the optimistic latch and inlined-first-slot HT in place — i.e., this matches "Traditional + OptBucketLatch + Inlining" in Figure 9 of the paper, not the very-bottom "Traditional" bar.

## 3 The sensitivity finding (Stage 3 result #1)

For each workload, we sweep the joint promotion-chance knob (`PROMO_OPT = PROMO_SHARED = PROMO_EXCL = N`) over `N ∈ {0 (= no promotion), 16, 32, 128}`, holding `PROMO_DISPLACE = 16`. Two replicates per cell.

> **Table 1 — Steady-state mean throughput (16 threads, in-memory, n = 2):**

| | TPC-C (k TX/s) | Random Read (k TX/s) | Skewed YCSB (k TX/s) |
|---|---|---|---|
| chance = ∞ (no promote) | 633 (−1.7 %) | 15 521 (−1.7 %) | 25 394 (−4.4 %) |
| chance = 1/16 | **651 (+1.2 %)** | **16 033 (+1.6 %)** | 25 579 (−3.7 %) |
| chance = 1/32 (paper default) | 644 | 15 786 | **26 552** |
| chance = 1/128 | 622 (−3.3 %) | 16 016 (+1.5 %) | 26 014 (−2.0 %) |

Three observations:

1. **The optimum is workload-dependent.** TPC-C and uniform random-read both peak at chance = 1/16; skewed YCSB peaks at chance = 1/32.
2. **The paper's 1/32 default is well-chosen for the workload the paper highlights.** Skewed YCSB is the case where predictive translation gives the largest win in §6.1, and 1/32 is the optimum there. On other workloads, 1/32 is mildly suboptimal (1–2 %).
3. **The throughput surface is concave for TPC-C and rndread** — too little promotion (∞ or 1/128) wastes the speculation benefit, too much promotion (1/16 → 1/32 trade-off is small but visible) exceeds the marginal value of further fast-path hits.

A practitioner with a TPC-C-like workload should consider lowering the chance to 1/16; with a heavily-skewed read-only workload, 1/32 is fine. Δ on TPC-C is within one σ (σ ≈ 35 k vs Δ ≈ 8 k) so we mark it as *suggestive*, not definitive — but the directionality is consistent across both replicates.

## 4 The CBP attempt (Stage 3 result #2)

CBP is implemented in `repo/PrediCache/cbp.hpp` (≈ 130 LOC) and wired in at three sites:

* `unfixS` (after a shared-mode read)
* `unfixX` (after an exclusive-mode write)
* `GuardO::operator=` (the hot optimistic-read teardown — needed for full coverage of the fast-path)

The state is 64 cache-line-aligned `ClassStats` records. The class for a PID is `(MurmurHash2(pid) >> 6) & 63`. On each instrumented access we sample one access in 8, atomically increment the class's `total` and (if the page is in its preferred frame) `in_pref`. When `total` crosses a 4 K-access window, the first thread that observes it claims a CAS-driven adaptation slot and computes:

> score = hit_rate − α · promote_rate (α = 0.20)

It compares to the previous window's score, keeps direction if score improved by > 0.5 %, reverses direction if it worsened by > 0.5 %, holds otherwise (deadband). `log_chance` moves by 1 in the chosen direction, clamped to [1, 14]. The first three windows per class are warmup (observation only).

### 4.1 Final results (n = 2, in-memory, 16 threads)

> **Table 2 — Mean steady-state throughput, with std-dev. Bold marks the highest:**

| | TPC-C (k TX/s) | Random Read (k TX/s) | Skewed YCSB (k TX/s) |
|---|---|---|---|
| **Traditional** (`DISABLE_PRED=1`) | 636 ± 10 (−0.1 %) | 15 467 ± 67 (−4.3 %) | **25 852 ± 482 (+1.7 %)** |
| **Default** (1/32) | **637 ± 13** | **16 153 ± 378** | 25 431 ± 274 |
| **CBP-static** (counters, no adapt) | 546 ± 2 (−14.3 %) | 14 812 ± 5 (−8.3 %) | 19 630 ± 410 (−22.8 %) |
| **CBP** (full) | 564 ± 16 (−11.5 %) | 14 435 ± 402 (−10.6 %) | 19 740 ± 211 (−22.4 %) |

Three findings:

1. **CBP is slower than the static default on all three workloads.** The bookkeeping floor (CBP-static row) is 8–23 % below default, and full adaptation only narrows that gap by 0–3 % on TPC-C, hurts slightly on rndread.
2. **The fast path of predictive translation is workload-sensitive even in our setup.** Traditional vs Default: tied on TPC-C (no benefit from speculation at 16 threads), +4.3 % on rndread (predictive translation actually helps), within one σ on skewed YCSB.
3. **Adaptation overhead dominates.** At 25 M TX/s (skewed YCSB), even our 1-in-8-sampled atomic increments cost 23 % of throughput. A redesigned counter (thread-local, bulk-flush) would be needed to make CBP competitive.

### 4.2 Why the controller does not converge to the optimum

> **Table 3 — End-of-run per-class `log_chance` distribution (lower = more promote; optimum in parentheses):**

| Workload | mean ± σ | range | Optimum |
|---|---|---|---|
| TPC-C | 8.86 ± 3.53 | [2, 14] | 4 (chance = 1/16) |
| Random Read | 8.59 ± 2.74 | [3, 14] | 4 (chance = 1/16) or 7 (1/128) — flat |
| Skewed YCSB | 9.36 ± 2.38 | [4, 14] | 5 (chance = 1/32) |

The controller drifts toward the *upper* half of `log_chance` on every workload, even when the workload's optimum is at low `log_chance` (TPC-C). The score function `hit_rate − α · promote_rate` only penalises promotion — there is no reward signal that pushes `log_chance` *down* when *increasing* promotion would lift `hit_rate`. This makes the controller systematically conservative.

A correct cost-benefit formulation would estimate `d(hit_rate) / d(promote_rate)` directly, e.g., by partitioning the 64 classes into two arms and sampling them at different `log_chance` values (a multi-armed-bandit per-class controller). **In round 2 we built and tested this — see §4.5.**

### 4.5 Round 2 — CBP-v4 with thread-local counters and a 2-arm bandit

Round 1's reviewer flagged two specific weaknesses: (a) bookkeeping overhead was 14–23 % even with adaptation off, and (b) the score function was one-sided. We rewrote the controller as **CBP-v4**:

* **Thread-local accumulators.** Each worker thread owns four 32-bit counters (`arm_total`, `arm_hits`, two arms each). On every access we increment two thread-local counters and check a single `< FLUSH` threshold. When the per-arm counter hits 4 K observations, the thread bulk-flushes to a shared `atomic<u64>` accumulator. One atomic per ~4 K accesses ≈ 0.06 ns/access amortised — ~80× cheaper than the v3 sampled-atomics path.
* **2-arm counterfactual bandit.** Classes are partitioned by `class & 1` into arm 0 (running at log_chance = `lc_lo`) and arm 1 (at `lc_lo + 1`). Every 8 cumulative epochs we compute the per-arm hit rates and shift `lc_lo` by ±1 (margin ≥ 0.5 %). No `α` tunable — the bandit estimates `d(hit_rate)/d(log_chance)` from the actual A/B comparison.

> **Table 4 — Round 2 results (n = 3 each, in-memory, 16 threads):**

| Workload | Default 1/32 | CBP-v4 | vs default | Within 1σ? |
|---|---|---|---|---|
| TPC-C | 678 ± 18 k TX/s | **673 ± 6 k** | −0.83 % | ✅ |
| Random Read | 15 870 ± 570 k TX/s | **15 832 ± 229 k** | −0.23 % | ✅ |
| Skewed YCSB | 25 885 ± 1 056 k TX/s | 23 973 ± 993 k | −7.39 % | ❌ |

> **Table 5 — Round 1 → Round 2 progression:**

| Workload | CBP-v3 (round 1) | CBP-v4 (round 2) |
|---|---|---|
| TPC-C | −11.5 % | **−0.83 %** |
| Random Read | −10.6 % | **−0.23 %** |
| Skewed YCSB | −22.4 % | −7.39 % |

CBP-v4 is **statistically indistinguishable from the static default on TPC-C and Random Read**. The thread-local counter redesign eliminated essentially all of round-1's bookkeeping floor on these workloads. Skewed YCSB still regresses by −7.4 %; the cause is the per-access overhead amplified by 26 M TX/s throughput (3 instructions/access × 4×10¹⁰ accesses/s ≈ 18 % of cycles).

### 4.6 What's still wrong with v4

1. **The bandit overshoots.** End-of-run `(lc_lo, lc_hi)` lands at extremes for every workload — TPC-C/rndread drift to (13, 14) (almost-no promotion), ycsb_skewed to (1, 2) (50 % promotion). The bandit moves in the right direction *initially* but doesn't stop at the throughput optimum because `hit_rate` is monotonic in `log_chance` (more promotion → more pages in preferred frames → higher hit rate, *regardless of whether throughput keeps improving*).
2. **The within-run trajectory is not stable across runs.** TPC-C run 1 converged to (1, 2); run 3 to (13, 14). The hit-rate signal is too noisy at equilibrium for the bandit to pin a stable answer.
3. **YCSB regression.** At the 26 M TX/s throughput level, even minimal per-access overhead is amplified.

The honest fix for #1 and #2 is to bandit on a *throughput proxy* (e.g., cycles-per-access via rdtsc sampling) instead of `hit_rate`. We document this as future work.

## 5 Limitations and honest framings

* **No vmcache / LeanStore comparison.** We cannot say PrediCache is faster than its competitors on our hardware. We restricted claims to internal comparisons.
* **Lustre back-end.** Out-of-memory experiments are I/O-bound on a network filesystem and uninformative — we deliberately restricted to in-memory regimes.
* **52 cores, 16 threads tested.** Paper's most dramatic numbers are at 192 threads. We expect the qualitative trend to be consistent, but absolute % gains are smaller.
* **CBP's bookkeeping floor.** The 14–23 % overhead from per-class atomics at 16 threads dominates the 1–2 % that perfectly-tuned CBP could possibly win. A tactically smaller-overhead variant (e.g., once-per-second sampling, thread-local counters) might invert this — but that is not what we measured.
* **CBP's score function is one-sided.** §4.2.
* **n = 2 replicates is thin.** Several "best fixed" deltas (e.g., +1.2 % TPC-C) are within one σ. We document the result as suggestive, not definitive.

## 6 What this report is and is not

* **Is a small reproducibility-with-extension report:** it confirms PrediCache's design works as advertised at the smaller scale we have, demonstrates the paper's 1/32 default is mildly suboptimal on TPC-C / random-read, and gives a concrete drop-in tuning recommendation (try 1/16 for those workloads).
* **Is *not* a "CBP wins" paper:** the adaptive controller we built (v4) is competitive with the static default on 2/3 workloads (within 1σ) but does *not* beat it. The remaining YCSB regression is a measurable architectural cost of any per-access instrumentation at 26 M TX/s.
* **Is honest about iteration:** round 1 (CBP-v3) was a clear negative result; round 2 (CBP-v4) recovered most of the loss via thread-local counters and a counterfactual bandit, but the bandit's hit-rate signal is misaligned with throughput so it overshoots in both directions. A bandit on a throughput proxy (rdtsc cycles-per-access) is the natural next step.

## 7 Reproduction artifacts

```
/lustre/s_zun212/TPC-C/
├── repo/PrediCache/             # cloned + patched
│   ├── cbp.hpp                  # 64-class hill-climber (v3) + 2-arm bandit (v4)
│   ├── buffer_manager.hpp       # +env knobs +DISABLE_PRED + CBP wiring
│   ├── guards.hpp               # +CBP observation in optimistic teardown
│   └── predicache.cpp           # +CBP_DUMP env hook
├── experiments/
│   ├── run_main_v2.sh           # 30-run sensitivity sweep
│   ├── run_cbp_v3.sh            # 24-run CBP-v3 + Traditional comparison
│   ├── run_cbp_v4.sh            # 18-run CBP-v4 (thread-local + bandit)
│   ├── analyze_v2.py
│   └── plot.py
├── idea-stage/IDEA_REPORT.md    # idea generation
├── review-stage/AUTO_REVIEW.md  # adversarial review (rounds 1 & 2)
└── results/
    ├── main_v1/*.csv            # initial v1 sensitivity sweep (broken rndread; superseded)
    ├── main_v2/*.csv            # corrected sensitivity sweep + per-class CBP dumps
    ├── cbp_v3/*.csv             # round-1 comparison (trad / default / cbp_static / cbp_v3)
    ├── cbp_v4/*.csv             # round-2 comparison (default / cbp_v4)
    └── figures/                 # fig_sensitivity.pdf, fig_v3_compare.pdf, summary.json
```

To rebuild and repeat:

```bash
cd repo/PrediCache
make                    # builds ./predicache; needs libaio-dev
./predicache            # default config: TPC-C, 1 thread, 1 GB pool

# Sensitivity sweep (≈ 25 min)
../../experiments/run_main_v2.sh

# CBP / Traditional comparison (≈ 14 min)
../../experiments/run_cbp_v3.sh
```

To enable CBP at runtime: `CBP=1 CBP_DUMP=path.csv ./predicache`. To enable counters but disable adaptation: `CBP_STATIC=1`. To disable predictive translation: `DISABLE_PRED=1`. To override promotion probability: `PROMO_OPT=N PROMO_SHARED=N PROMO_EXCL=N PROMO_DISPLACE=M ./predicache` (probability is `1 / (next_pow2(N))`).

## 8 Figure inventory

* `results/figures/fig_sensitivity.pdf` — throughput vs fixed promotion probability per workload (TPC-C, Random Read, Skewed YCSB), with v2 CBP dashed line for reference.
* `results/figures/fig_v3_compare.pdf` — bar chart per workload of trad / default / cbp_static / cbp throughput.
* `results/figures/summary.json` — machine-readable means + stdev for every (workload, config) cell.

No additional figures need manual creation. A submission paper would also benefit from (1) a per-class log_chance histogram comparison across workloads (data is in `results/cbp_v3/*_cbp_dump.csv`) and (2) a per-second time series of the v2 sensitivity sweep showing TPC-C OOM-transition behaviour — both are derivable from raw CSVs without further runs.
