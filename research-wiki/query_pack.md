# Query Pack — PrediCache buffer-management / adaptive promotion

## Project direction (300c)

Reproduce PrediCache (SIGMOD 2026) on commodity hardware (16-thread Xeon, Lustre) and design an adaptive replacement for its hand-picked promotion constants (1/32, 1/512). Constraint: no vmcache/LeanStore comparison (kernel-module access denied, integration cost too high).

## Top gaps (≤ 1200c)

* **G1 — Hand-picked promotion probabilities (partial).** PrediCache picks 1/32 and 1/512 without adaptation. Targeted by idea:CBP. v4 reached break-even on 2/3 workloads (TPC-C/rndread within 1σ; YCSB still −7.4 %). Remaining work: throughput-proxy bandit instead of hit_rate-based.
* **G3 — Sampling-based eviction unimplemented.** PrediCache claims its metadata layout enables WATT/HyperBolic but never demonstrates. Cheap follow-on for a workshop paper.
* **G5 — Set-associative predictive translation.** Uniform random read is paper's worst case; K candidate frames per PID (cache-set analogy) might help.
* **G7 — Right signal for adaptive promotion.** hit_rate is monotonic in log_chance → any hit-rate bandit overshoots. cycles/access (rdtsc) would be the right proxy.
* **G2, G4, G6 — variable-size pages, NUMA, cloud cost model.** Each is its own paper.

## Paper clusters (≤ 1600c)

**Cluster A — Modern buffer-pool designs (all alternatives to hash-table-based pools):**
* paper:zinsmeister2026_predictive_translation (hash table + speculation + deterministic preferred frame; **our core**)
* paper:leis2023_vmcache (OS page table + exmap kernel module — fast but practically unusable)
* paper:leis2018_leanstore (pointer swizzling — single-reference constraint)

**Cluster B — Adaptive caching (orthogonal but novelty-relevant):**
* paper:anonymous2025_adaptiveclimb (LRU-list promotion-distance adaptation; distinct mechanism)

## Failed ideas (always preserved, ≤ 1400c)

* **idea:CBP-v3 — Per-class hill climber with `hit_rate − α·promote_rate` score: FAILED**
  - **Why:** (a) per-class atomic counters cost 14–23 % bookkeeping alone (claim:C2); (b) score function is one-sided — only penalises promotion, never rewards it, so controller drifts toward "less promote" on every workload regardless of throughput optimum.
  - **Lesson — banlist:** Don't ship per-access atomic instrumentation in a high-throughput hot path. Don't use `hit_rate − α·promote_rate` style scores for promotion control — use counterfactual A/B with throughput proxy.

* **idea:CBP-v4 — Thread-local counters + hit_rate-based 2-arm bandit: PARTIAL.** Matches default on TPC-C/rndread within 1σ but loses 7.4 % on Skewed YCSB. **Why YCSB:** at 26 M TX/s the residual per-access bookkeeping (3 instructions/access) costs ~7 %. **Why doesn't stop at optimum:** hit_rate is monotonic in log_chance — bandit walks to extremes (lc=1 or lc=14) rather than stopping where throughput peaks.

## Top papers (ranked, ≤ 1800c)

1. **paper:zinsmeister2026_predictive_translation** — central paper; describes the design, motivates the gap, lists its own future work (G2–G6). Relevance: core.
2. **paper:leis2023_vmcache** — first of three baselines PrediCache benchmarks against. Used to understand the "we don't need a kernel module" argument. Relevance: related.
3. **paper:leis2018_leanstore** — pointer-swizzling baseline, illuminates the multi-reference constraint that PrediCache positions itself to relax. Relevance: related.
4. **paper:anonymous2025_adaptiveclimb** — peripheral; used for novelty check on CBP. Distinct mechanism (LRU list vs frame placement). Relevance: peripheral.

## Active chains (≤ 900c)

* **chain-1**: paper:zinsmeister2026_predictive_translation (limitation: hand-picked constants) → gap:G1 → idea:CBP → exp:E2 (invalidated v3) → exp:E3 (v4 partial) → gap:G7 (new gap surfaced: hit_rate is wrong signal).
* **chain-2**: paper:zinsmeister2026_predictive_translation (limitation: sampling-eviction promised, not delivered) → gap:G3 (unresolved opportunity).

## Open unknowns (≤ 500c)

* How does CBP-v4 behave at 32, 48, 52 threads? Untested.
* Throughput-proxy bandit (cycles/access) — would it stop at the optimum? Untested.
* Per-class log_chance histogram on real OLTP traces (not synthetic TPC-C) — would the optimum land at 1/16 or somewhere else? Unknown.
