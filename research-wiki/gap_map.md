# Field Gaps (PrediCache buffer-management line)

| ID | Gap | Status | Linked ideas | Linked papers |
|---|---|---|---|---|
| **G1** | PrediCache uses **hand-picked promotion probabilities** (1/32, 1/512) with no workload adaptation mechanism. | **partial** — addressed by idea:CBP (v4 reaches break-even on 2/3 workloads). | idea:CBP | paper:zinsmeister2026_predictive_translation |
| G2 | Variable-sized pages are explicitly out of scope (PrediCache §7). A tiered buffer pool (Bf-Tree / Umbra style) is hinted as a starting point. | unresolved | — | paper:zinsmeister2026_predictive_translation |
| G3 | Sampling-based eviction (WATT, HyperBolic) is *claimed* to be enabled by PrediCache's per-frame metadata layout but **never demonstrated**. | unresolved | — | paper:zinsmeister2026_predictive_translation |
| G4 | No comparison against a NUMA-aware variant — PrediCache runs single-socket. | unresolved | — | paper:zinsmeister2026_predictive_translation |
| G5 | Random uniform reads are the paper's worst case (91 % preferred-frame hit but no superscalar benefit). Set-associative predictive translation (K candidate frames) is not explored. | unresolved | — | paper:zinsmeister2026_predictive_translation |
| G6 | Cloud-native cost model — adapting the five-minute rule to PrediCache-style caches is mentioned as future work but not addressed. | unresolved | — | paper:zinsmeister2026_predictive_translation |
| G7 | **The right signal for adaptive promotion.** hit_rate is monotonic in promote_rate so any bandit on hit_rate overshoots; cycles/access (rdtsc-sampled) would be the right proxy but is unimplemented. | unresolved (uncovered by exp:E3) | — | (this work — round 2 finding) |
