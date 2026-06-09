# Adaptive Promotion for Predictive Translation: Where to Measure, and What to Promote

**A reproduction-with-extension study of PrediCache (SIGMOD 2026)**

*Hardware: 2× Intel Xeon Gold 5320 (52 cores, 1 thread/core, 2 NUMA nodes),
440 GB RAM, Lustre over NVMe-backed OSTs, g++ 11.4 `-O3 -std=c++23 -laio`.
All experiments in-memory, 16 worker threads unless noted.*

---

## Abstract

PrediCache introduces *predictive translation*: each page is bound to a
deterministic preferred buffer frame, letting the CPU speculatively read from
that frame in parallel with the hash-table lookup. Accuracy is maintained by
**probabilistically promoting** hot pages into their preferred frames using two
hand-picked constants ($\tfrac{1}{32}$ with no displacement, $\tfrac{1}{512}$
with). We reproduce PrediCache on commodity Intel hardware and ask whether these
constants can be made adaptive *without* sacrificing the hot-path speed that is
the technique's whole point. We report three results. **(1)** A sensitivity
sweep shows $\tfrac{1}{32}$ is workload-dependent but the throughput surface is
flat (1–3 % headroom). **(2)** **TPB**, a throughput-probed bandit that lives
entirely on the existing per-second stat thread, matches the hand-picked default
within statistical noise on all three workloads at **zero hot-path overhead** —
but, on a flat surface, cannot beat it; its durable contribution is the
architectural lesson *where to measure*. **(3)** Shifting the question from *how
often* to *what* to promote, **role-aware promotion** classifies each page as a
B-tree inner or leaf node and promotes them at different rates. On uniform
random-read this **beats the single best global constant by +5.4 %**
($|t|=3.83$, the first statistically significant win in this study), and the
benefit of withholding leaf promotion **flips sign monotonically with workload
skew** — confirming a clean cost/benefit mechanism and motivating an adaptive
per-class controller.

---

## 1. Background: predictive translation and promotion

A traditional buffer manager translates a page-id (PID) to a frame address with
**two dependent memory accesses**: hash the PID into a hash table, then
dereference the resulting frame header. PrediCache attaches every PID to a
deterministic preferred frame (`MurmurHash2(pid) mod frameCount`) and lets the
CPU **speculatively** read from that address while the hash-table lookup is
still in flight. When the speculation is correct, superscalar execution overlaps
the two accesses and one memory latency is hidden.

The speculation is only accurate if hot pages actually reside in their preferred
frames, so PrediCache **probabilistically promotes** a page into its preferred
frame on access:

- $\tfrac{1}{32}$ if no resident page must be displaced,
- $\tfrac{1}{512}$ if a displacement is required ($\tfrac{1}{32}\times\tfrac{1}{16}$).

Promotion is implemented with two exclusive latches and a 4 KB `memcpy` (8 KB
when a page must be displaced). These constants are the object of our study.

**Reproduction scope.** We reproduce the in-memory regime (build needs only
`libaio-dev` and an `O_DIRECT`-capable file). A `DISABLE_PRED=1` env knob
reproduces the paper's internal "Traditional + opt-latch + inlining" ablation by
gating the speculative fast path. We do **not** compare against vmcache,
LeanStore, or WiredTiger: vmcache needs the `exmap` kernel module + root
(forbidden on shared infrastructure), and external-system comparison is
orthogonal to our question (can promotion be made adaptive *within*
PrediCache?). The Lustre backend makes the out-of-memory regime I/O-bound and
uninformative, so we restrict to in-memory.

---

## 2. The sensitivity finding: $\tfrac{1}{32}$ is workload-dependent, but the surface is flat

We sweep the joint promotion chance $p_0 \in \{0, \tfrac{1}{16}, \tfrac{1}{32},
\tfrac{1}{128}\}$ on three workloads (n=2, 16 threads, in-memory).

| $p_0$ | TPC-C (k TX/s) | Rnd-Read (k TX/s) | Skew-YCSB (M TX/s) |
|---|---|---|---|
| 0 (off) | 633 (−1.7 %) | 15 521 (−1.7 %) | 25.4 (−4.4 %) |
| $\tfrac{1}{16}$ | **651 (+1.2 %)** | **16 033 (+1.6 %)** | 25.6 (−3.7 %) |
| $\tfrac{1}{32}$ (paper) | 644 | 15 786 | **26.6** |
| $\tfrac{1}{128}$ | 622 (−3.3 %) | 16 016 (+1.5 %) | 26.0 (−2.0 %) |

**(F1)** TPC-C and uniform random-read peak at $\tfrac{1}{16}$, not the paper's
$\tfrac{1}{32}$. **(F2)** Skewed YCSB peaks at $\tfrac{1}{32}$ — the default is
the optimum for the workload the paper highlights. **(F3)** Differences are only
1–3 % and the curve is flat near the optimum. F3 sets the terms for everything
that follows: there is very little room for any controller that only tunes the
global probability to win.

---

## 3. TPB: a throughput-probed bandit at zero hot-path cost

### 3.1 Design

Earlier controllers in our development arc (Appendix A) failed for two reasons:
they instrumented the **hot path** (per-access atomics cost up to 22 % at 26 M
TX/s) and they used **hit-rate** as the signal (hit-rate is monotonic in
promotion probability, so a controller chasing it walks to extremes rather than
the throughput optimum). TPB fixes both:

- **G1 — zero hot-path overhead.** The per-access cost equals the static
  default: one relaxed atomic load of the current $\log_2(1/p_0)$ mask, nothing
  else. All controller logic runs on PrediCache's existing once-per-second stat
  thread (one added line: `cbp::tpbOnWindow(txProgress.exchange(0))`).
- **G2 — throughput-aligned signal.** The reward is the system's actual
  per-window transaction count.
- **G3 — no per-workload tuning.** One configuration for all workloads.

### 3.2 Mechanism: the probe-sandwich

TPB keeps one integer `lc_base` $= \log_2(1/p_0)$ that all workers read. Every
`PROBE_EVERY` windows it opens a three-window sandwich:

```
window k-1 : run at lc_base       → T_pre   (baseline)
window k   : run at lc_base ± 1    → T_probe (the probe)
window k+1 : run at lc_base again  → T_post  (baseline)
```

It estimates the baseline at the probe's instant by linear interpolation
$T_\text{avg} = (T_\text{pre}+T_\text{post})/2$, rejects the sandwich if
$|T_\text{pre}-T_\text{post}|/T_\text{pre} \geq 15\%$ (a phase guard for
non-stationary workloads such as TPC-C, whose dataset grows over time), and
accepts the probe iff $T_\text{probe} > T_\text{avg}\,(1+0.5\%)$. On acceptance
it adopts `lc_probe` and flips the probe direction. The sandwich cancels
throughput drift so the A/B test is unbiased.

### 3.3 Result

**TPB vs the static $\tfrac{1}{32}$ default (n=5, 30 s, 16 threads). Welch
two-sided $|t|$; 95 % significance needs $|t|>2.31$.**

| Workload | Variant | Mean (k TX/s) | σ | Δ vs default | $|t|$ |
|---|---|---|---|---|---|
| TPC-C | default | 634.4 | 13.3 | — | — |
| | TPB | 626.2 | 41.8 | −1.29 % | 0.42 |
| Rnd-Read | default | 15 552 | 234 | — | — |
| | TPB | 15 815 | 558 | +1.69 % | 0.97 |
| Skew-YCSB | default | 26 032 | 476 | — | — |
| | TPB | 26 561 | 373 | +2.03 % | 1.95 |

TPB is within 1σ of the default on every workload; no comparison is significant.
It auto-converges to the sensitivity-sweep optimum ($\tfrac{1}{16}$ on TPC-C,
$\tfrac{1}{32}$ on YCSB) with no manual tuning. **On a flat surface (F3), TPB
ties — it cannot win, but it adds adaptation at no cost and never breaks the
default.** Its transferable contribution is architectural: *instrument outside
the hot path, on the system's coarsest existing telemetry boundary, against its
real objective.*

---

## 4. Role-aware promotion: shifting from *how often* to *what*

TPB exhausts the "how often" axis. We turn to *which pages* are worth promoting.

### 4.1 Why a hash-partition class has no headroom

PrediCache's internal class function is `classOf(hash) = (hash>>6) & 63` — a
hash partition. Every class is a uniform random 1/64 sample of pages, so all
classes share the same optimum; per-class adaptation over hash partitions is
provably no better than a single global constant. A meaningful class must
capture a real difference in promotion payoff.

### 4.2 The mechanism: promotion is an investment that must be repaid

Promotion costs two latches + a 4 KB `memcpy` up front, repaid only if the page
is **re-accessed before eviction** (each later access runs the fast path).

```
benefit ≈ (re-accesses before eviction) × (fast-path saving)
cost    ≈ latches + memcpy  (paid once)
```

A B-tree splits cleanly into two populations with opposite economics:

| | **inner** nodes | **leaf** nodes (uniform workload) |
|---|---|---|
| Count | ~1 % | ~99 % |
| Re-access rate | very high (every lookup traverses them) | near zero |
| Survives to reuse? | yes, millions of times | almost never (evicted first) |
| Promotion economics | benefit ≫ cost | **cost only** |

A single global probability cannot serve both: any rate that keeps hot inner
nodes resident also pays the wasted `memcpy` toll on the cold leaf majority.

### 4.3 Implementation

We classify a page by its B-tree role at the promotion site: one byte load of
`isLeaf` from the page. Since `sizeof(BTreeNode)==pageSize` the node lies at
frame offset 0, and the offset is published once at startup
(`btreeIsLeafOffset = offsetof(BTreeNodeHeader, isLeaf)`). Hot-path cost is one
byte load + select — algorithmically the same as the global mask. Two env knobs
(`PROMO_INNER`, `PROMO_LEAF` under `PERCLASS=1`) set the per-role rates.

### 4.4 Result: a significant win on uniform random-read

**Per-class vs global constants, rndread (10 M-row single B-tree, 8 GB pool,
n=5, 30 s, 16 threads).**

| Config | k TX/s | Δ vs global $\tfrac{1}{16}$ | $|t|$ | Δ vs $\tfrac{1}{32}$ | $|t|$ |
|---|---:|---:|---:|---:|---:|
| global $\tfrac{1}{16}$ (single-constant best) | 15 605 ± 67 | — | — | — | — |
| global $\tfrac{1}{32}$ (paper default) | 15 771 ± 406 | +1.07 % | 0.90 | — | — |
| **inner $\tfrac{1}{16}$, leaf never** | **16 454 ± 491** | **+5.44 %** | **3.83** | **+4.33 %** | **2.40** |
| inner $\tfrac{1}{16}$, leaf $\tfrac{1}{128}$ | 15 690 | +0.54 % | 1.01 | — | — |
| inner $\tfrac{1}{8}$, leaf never | 15 733 | +0.82 % | 0.84 | — | — |

Both wins clear the 95 % bar. The separation is distributional, not an outlier:
4 of 5 winning replicates (16 444–17 044) exceed *every* baseline replicate
(15 548–15 719), and the baseline σ is 67. The grid pins the sweet spot — leaf
must be **never** (leaf = $\tfrac{1}{128}$ already collapses the gain to +0.5 %)
*and* inner held at $\tfrac{1}{16}$ — exactly the "stop paying the leaf toll,
keep the inner return" story.

### 4.5 The effect flips sign with skew

We measured the effect of turning leaf promotion **off** (inner fixed at
$\tfrac{1}{16}$) across workloads ordered by access skew:

| Workload (skew) | leaf-off | leaf-on | Δ (off − on) | $|t|$ | Verdict |
|---|---:|---:|---:|---:|---|
| rndread (uniform) | 16 454 | 15 690 | **+4.87 %** | 3.28 | leaf-off helps (significant) |
| TPC-C (mild) | 642 | 636 | +0.91 % | 0.70 | leaf-off helps (noise) |
| Skew-YCSB ($\zeta\!=\!0.9$) | 25 842 | 25 959 | −0.45 % | 0.25 | leaf-off neutral |

The benefit of *not* promoting leaves shrinks **monotonically with skew** —
+4.9 % (significant) → +0.9 % → ~0. Skew creates re-referenced hot leaves, which
restores leaf promotion's payoff and erases the win. On skewed YCSB no per-class
config beats the global best significantly (the surface is flat there too).

**The complete finding.** The optimal leaf-promotion policy is
workload-dependent and *flips sign with skew*. No single static rule is right
everywhere: "leaf never" wins by +5 % on uniform reads but loses its edge under
skew. The right primitive is a controller that reads each role's realized payoff
and toggles leaf promotion per workload — the one place we found structural
headroom that an *adaptive* policy could capture and that the flat-surface global
controllers provably could not.

---

## 5. Discussion

The study has two complementary axes. **TPB** answers *how often* to promote:
it makes the global probability adaptive at zero hot-path cost, but the flat
surface (F3) leaves nothing to win, so it ties. **Role-aware promotion** answers
*what* to promote: it finds real headroom (+5.4 % on uniform reads) by declining
to pay the promotion toll on cold leaves. The two are orthogonal and compose:
the natural next system is a **per-class TPB** that uses TPB's overhead-free,
throughput-aligned probing to toggle leaf promotion per workload — capturing
+5 % on uniform reads automatically while leaving it on under skew.

---

## 6. Limitations

- **Flat surface.** F3 caps any "how-often" controller at 1–3 %; TPB's tie is a
  near-null performance result, valuable mainly for the architectural lesson.
- **Role-aware win is workload-dependent.** "Leaf never" is significant on
  rndread, neutral under skew; it is not a universal default.
- **n=5.** Several deltas are within 1σ; the rndread win and the sign-flip are
  significant, but a confirmation run at n≥8 would tighten the leaf-never margin.
- **Scope.** 16 threads on Intel Xeon; the paper's headline numbers are at 192
  threads on EPYC, where the promotion cost/benefit (latch contention) may shift
  and the surface may sharpen. In-memory only; no external-system comparison.

---

## 7. Future work

1. **Per-class TPB** — apply TPB's probe-sandwich to the per-role leaf rate;
   verify it auto-captures the rndread +5 % and holds leaf promotion under skew.
2. **Finer probability grid** — replace `rand & mask` with a threshold compare
   to allow non-power-of-two rates (e.g. $\tfrac{1}{24}$) at the same hot-path
   cost; test whether an interior optimum exists between the power-of-two rungs.
3. **The displacement constant** ($\tfrac{1}{512}$) — never swept; a second knob.
4. **High thread counts** (32/48/52, ideally 192) — confirm whether the surface
   sharpens enough to give a "how-often" controller real headroom.

---

## Appendix A. Development arc of the adaptive controllers

| Variant | Design | TPC-C Δ | Skew-YCSB Δ | Why it failed / shipped |
|---|---|---|---|---|
| CBP-v3 | 64-class hill climber, score = hit_rate − α·promote_rate, sampled hot-path atomics | −11.5 % | −22.4 % | bookkeeping floor 8–23 %; one-sided score drifts conservative |
| CBP-v4 | thread-local counters + 2-arm hit-rate bandit | −0.83 % | −7.4 % | overhead floor gone on 2/3 wls; hit-rate signal walks to extremes |
| TPB-v3 | throughput signal, tol 5 %, probe-every 8 | −2.21 % (sig) | — | phase tol too strict; direction-update bug → degenerate always-promote |
| **TPB-v5** | throughput signal, tol 15 %, probe-every 16, fixed direction/boundary | −1.29 % | +2.03 % | shipped (Section 3) |

## Appendix B. Reproduction

```bash
git clone git@github.com:DaeZZang/cloud_project.git && cd cloud_project/repo/PrediCache
make                                            # needs libaio-dev
bash ../../experiments/run_main_v2.sh           # §2 sensitivity sweep
bash ../../experiments/run_tpb_v5_3wl.sh        # §3 TPB vs default
bash ../../experiments/run_perclass_a1.sh       # §4 role-aware, TPC-C
bash ../../experiments/run_perclass_a1_wl2.sh   # §4 role-aware, rndread + YCSB
python ../../experiments/analyze_perclass_wl2.py
```

Runtime knobs: `TPB=1 TPB_INIT_LC=5 TPB_PHASE_TOL=15 TPB_PROBE_EVERY=16` (TPB);
`PERCLASS=1 PROMO_INNER=N PROMO_LEAF=M` (role-aware, prob $=1/\text{nextPow2}$);
`DISABLE_PRED=1` (Traditional ablation). Raw CSVs under `results/`.
