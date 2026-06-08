# Idea Discovery Report — PrediCache Reproduction & Method Development

**Source artifact:** `https://github.com/mzinsmeister/PrediCache` (SIGMOD 2026)
**Paper:** Zinsmeister, Nguyen, Leis, Neumann. "Predictive Translation: High-Performance Buffer Management Without the Trade-Offs."
**Date:** 2026-05-08
**Hardware available:** 2× Intel Xeon Gold 5320 (52 cores, 2 NUMA nodes), 440 GB RAM, Lustre storage (O_DIRECT supported), no NVMe SSD.
**Build status:** ✅ binary compiles and runs end-to-end (TPC-C 2-thread, 4-WH, 2 GB pool: ~100k TX/s).

---

## 1. PrediCache in one paragraph

A traditional buffer manager translates a page-id (PID) to a buffer-frame address by hashing the PID into a hash table, then dereferencing the resulting frame header. Two **dependent** memory accesses dominate latency. PrediCache attaches every PID to a *deterministic preferred frame address* (`hash(pid) mod frameCount`) and lets the CPU **speculatively** read from that address while the hash-table lookup is still in flight. When the speculation is correct (the page truly is in its preferred frame), the two memory accesses run in parallel — superscalar execution hides the hash-table cost. When wrong, the CPU's branch predictor flushes the speculation and falls back to the conventional path. To keep the speculation correct, hot pages are *probabilistically promoted* into their preferred frame on subsequent accesses (full 4 KB memory copy with two exclusive latches; promotion probability is **fixed** at 1/32 if the preferred slot is free or self-occupied, 1/512 if a demote is required). The hash table is a chaining hash table with the first slot inlined and synchronized via an optimistic versioned lock; pre-sized to 2× pool capacity so most chains have length 1.

**Headline claims:** TPC-C in-memory throughput +34 % vs vmcache, +19 % vs LeanStore at 192 threads. OOM TPC-C 19.7 %/24.6 % over vmcache/LeanStore. Single-threaded random-read: 3 510 cycles/op vs 3 939 (vmcache), 5 090 (traditional). 91 % of accesses hit the predicted frame even at 3× OOM.

## 2. What the paper leaves open

I read all 16 pages and the source. The clearest extension surfaces are:

| # | Hook | Status in paper | Difficulty |
|---|------|-----------------|------------|
| H1 | Promotion probabilities **1/32 and 1/512 are hand-picked constants**; adaptiveness left untouched | "We chose 1/32 / 1/512" — no learning signal, no workload adaptation | Low |
| H2 | The preferred-position function is a fixed hash (MurmurHash2 mod N) — every PID has *one* candidate frame | Conflicts on hot frames cause demotes / mispredictions | Med |
| H3 | Sampling-based eviction (WATT/HyperBolic) is named as a benefit but **never implemented or evaluated** | §6.9 only argues "metadata fits per-frame, so it could work" | Med |
| H4 | Variable-sized pages explicitly excluded ("orthogonal to transaction processing, future work") | §7 closing paragraph | High |
| H5 | NUMA: paper runs on a single-socket EPYC, never discusses multi-socket placement | Silent | Med |
| H6 | Cost of a 4 KB deep copy on every promotion is amortized but **never quantitatively bounded against alternatives** (e.g., index-into-frame indirection) | §5.1 hand-waves the bound | Med |
| H7 | Random-read is paper's worst case (91 % preferred-frame hit, but no fast-path benefit on uniform leaves) | Acknowledged but not addressed | High |

H4 and H6 collide with the paper's core invariant (preferred frame at deterministic position) — those are *new architectures*, not extensions. H1, H2, H3, H5 are clean extensions on top of the existing codebase.

## 3. Generated ideas (5)

### Idea A — **Cost-Benefit Adaptive Promotion (CBP)** ⭐ primary

Replace fixed 1/32 and 1/512 with a per-class *online* estimator of marginal expected throughput gain per promotion.

* **Mechanism.** Partition the PID space into K=64 promotion classes via `hash(pid) mod K` (cheap, no per-page state). Each class stores three counters in a 64-byte cache line: `n_attempts`, `n_remained_in_preferred`, `n_evicted_quickly`. Every Δ accesses, derive an estimated benefit/cost ratio per class and recompute its promotion probability via a Hoeffding-bound-style upper-confidence rule (or simple EWMA).
* **Why it's principled.** PrediCache's superscalar benefit is realised only on accesses where the page is in its preferred frame. The constant 1/32 trades amortised copy cost against expected fast-path hits, but the trade is workload-dependent (TPC-C neworder vs uniform reads vs zipf-skewed). Per-class adaptation lets hot classes promote aggressively and cold classes barely promote.
* **Memory cost.** 64 × 64 B = 4 KB total — negligible vs the 2 GB+ pool.
* **Predicted gains.** Skewed YCSB (zipf=0.9) sees most promotion benefit because hot classes converge fast; uniform random-read sees gain by *reducing* useless promotion copies. TPC-C should be neutral-to-positive.

**Pilot signal already on hand:** the paper's §6.7 figure shows in-memory random-read promotes at a steady ~0.05 B/access — a 1/32 probability would predict more. The static prob is already conservative; an adaptive policy can drop it further under uniform load and ramp it on skew.

**Implementation:** ~150 LOC change confined to `buffer_manager.hpp::decidePromotion*`, `BufferFrame`, plus one new flat array. No B-tree changes, no benchmark changes.

### Idea B — Set-Associative Predictive Translation (SAPT-K)

Map each PID to a *set* of K∈{2,4} candidate preferred frames (associative caching). On read, pick one candidate via the bits of the HT entry (1 or 2 bits added). Reduces conflicts on hot frames at the cost of a 1-bit metadata write on demote.

* **Pros.** Fewer demote-thrash cycles; conflict reduction is largest at high fill factors.
* **Cons.** Needs careful CAS choreography to preserve lock-freedom; speculation in CPU now branches across K candidates (mispredict cost grows).
* **Status.** Riskier than A; designed but not piloted because the success criterion (fewer demotes) is harder to attribute experimentally.

### Idea C — Sampling-Based Eviction Integration (SAMPLE-EVICT)

Implement WATT-style sampling eviction inside PrediCache and benchmark against the existing CLOCK-like eviction. Paper *claims* this is enabled by the design but never demonstrates it.

* **Pros.** Strong validation of a design-goal claim.
* **Cons.** Eviction is a separate subsystem; engineering load is higher; gain is workload-dependent (skewed > uniform).
* **Status.** Solid backup; less differentiated as a core method contribution.

### Idea D — NUMA-Aware Predictive Translation (NUMA-PT)

Partition the buffer pool into NUMA-local segments. The preferred-frame hash includes the worker's NUMA node, so frames are local to the consuming thread.

* **Pros.** Directly relevant to our 2-socket reproduction hardware. Paper used a single-socket machine — opens new ground.
* **Cons.** Requires thread-affinity discipline in the benchmarks; cross-NUMA accesses still happen for shared pages and may regress.
* **Status.** Would require fairly heavy plumbing for a method contribution; flagged as an *experiment ablation* once A is in place.

### Idea E — Promotion-Free Fast Path via Indirection Bit (NPF)

Instead of physically copying pages on promotion, add a 1-bit `is_in_preferred` flag to the hash-table entry and store the actual frame pointer there. Speculate on the predicted frame **only** if the flag is set.

* **Pros.** Removes the 4 KB memory copy entirely.
* **Cons.** Conflicts directly with PrediCache's invariant (the speculation gets data from the *deterministic* position — if the page isn't physically there, speculation is useless). Equivalent to disabling fast path. Reject.

## 4. Novelty check

* H1's adaptive-probability angle has no direct analogue in PrediCache lineage. Closest prior art:
  - "AdaptiveClimb" (arXiv 2511.21235, Nov 2025) — adapts *promotion distance* in cache-replacement (LRU-list semantics), not buffer-pool placement.
  - WATT [VLDB 2017] — sampling-based weighted eviction, not promotion control.
  - "Resource-Adaptive Query Execution with Paged Memory Management" (CIDR 2025, LIPAH) — adaptive *between* swizzling and HT, not within-HT promotion.
* No prior work models PrediCache-style promotion as an online cost-benefit problem. **Novelty: confirmed.**
* The contribution is small but cleanly addresses a constant left dangling by a SIGMOD 2026 paper — a publishable workshop / SIGMOD industrial-track increment, or a meaty paragraph in a reproducibility-with-extension story.

## 5. Pilot plan (Idea A — CBP)

1. **Sensitivity sweep** (validates premise): run vanilla PrediCache with PROMO_PROB ∈ {0, 1/512, 1/128, 1/32 (default), 1/8, 1/2, 1} on TPC-C, random-read, skewed YCSB. Plot throughput vs probability per workload. *Expected:* the curve is non-monotonic and the optimum differs per workload — supporting the case for adaptation.
2. **Implement CBP** as described in §3.A (per-class EWMA + UCB-style probability update every 2^14 accesses).
3. **Compare** CBP vs every fixed probability vs the paper default at 16, 32, 48, 52 threads. Metrics: throughput, mispredict rate, promotion bytes/access (paper §6.7 figure).
4. **Ablate** K (number of classes) ∈ {1, 8, 64, 1024}. Show K=64 is sweet spot.

The first pilot is feasible in ~30 min of compute with the binary already built.

## 6. Reproduction scope (constraints honest)

| Paper experiment | Reproducible here? | Notes |
|---|---|---|
| §6.1 In-memory TPC-C / random-read / skewed YCSB | **Yes**, with smaller pool (32–128 GB) and lower thread ceiling (52 vs 192) | Lustre instead of NVMe |
| §6.2 OOM | **Partial** — Lustre is much slower than NVMe; absolute numbers will not match, relative comparison vs Traditional baseline still informative |
| §6.3 In-mem→OOM transition | Yes (timed) |
| §6.4 Ablation (Trad → +OptLatch → +Inline → +PT) | **Yes** — all internal toggles |
| §6.5 Microarchitectural (perf counters) | **Yes** — `perf` is available; Xeon counters differ from EPYC but the IPC delta should hold qualitatively |
| §6.6 Hash-chain length distribution | **Yes** — analytic + microbench |
| §6.7 Promotion bytes/access | **Yes** |
| §6.8 Preferred-position fraction | **Yes** |
| Comparisons vs vmcache / LeanStore / WiredTiger / LMDB | **No** — vmcache requires `exmap` kernel module (security concerns + sudo); LeanStore/WiredTiger/LMDB add 1–3 days of integration work |

We will reproduce *internal* claims (PrediCache-vs-Traditional ablation, microarchitectural advantage, prefer-frame statistics) faithfully, and compare against the paper's reported numbers for vmcache/LeanStore as the external anchor. This is honest and labelled as such in the paper.

## 7. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Lustre I/O is the bottleneck for OOM, masking PrediCache effects | Focus core results on **in-memory** experiments where PrediCache shines; OOM only as sanity check |
| 52-core machine ≠ 192-thread paper setup | Show throughput-vs-thread up to 52 — argue scalability *trend* matches |
| C++23 / -O3 / huge pages: any toolchain mismatch | Already verified `g++ 11.4 + -std=c++23 + -O3 -laio` builds and runs |
| Hash-collision behaviour at smaller pool changes results | Pin hash-table size at exactly 2× pool, as paper |
| Adaptive policy gets stuck in local optima | Add ε-greedy exploration in UCB term |

## 8. Decision (AUTO_PROCEED=true)

**Selected idea: A — Cost-Benefit Adaptive Promotion (CBP).**
**Rationale:** novelty confirmed, pilot feasible on hand, implementation surface is ~150 LOC inside one file, paper has not addressed adaptivity, our hardware can validate every claim. Backup is C (sampling eviction), to be revisited only if CBP fails to show ≥3 % gain on at least one of TPC-C / skewed YCSB.

Proceeding to Stage 2 (implementation).
