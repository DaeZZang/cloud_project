---
type: idea
node_id: idea:CBP
title: "Cost-Benefit Adaptive Promotion (CBP)"
stage: tested
outcome: partial
based_on: ["paper:zinsmeister2026_predictive_translation"]
target_gaps: ["gap:G1"]
added: 2026-05-11T00:02:00Z
---

# Cost-Benefit Adaptive Promotion (CBP)

## Hypothesis

PrediCache's hand-picked promotion probabilities (1/32, 1/512) are workload-dependent suboptimal points. An online controller that adapts the probability based on observed access patterns will match or beat the static defaults without manual tuning.

## Mechanism (final form, v4)

* **Thread-local accumulators** — each worker thread owns `arm_total` and `arm_hits` counters for two arms. Increment is non-atomic; bulk-flush to a shared atomic accumulator once per ~4 K observations per arm.
* **2-arm counterfactual bandit** — PID classes split by `class & 1`; arm 0 runs at `log_chance = lc_lo`, arm 1 at `lc_lo + 1`. Every 8 cumulative flush epochs, compare the two arms' hit_rates; shift `lc_lo` by ±1 with a ≥ 0.5 % margin.
* **No `α` tunable** — the bandit estimates `d(hit_rate)/d(log_chance)` from the actual A/B comparison.

## Tested versions

| Version | Mechanism | Outcome |
|---|---|---|
| **v3** (round 1) | 64-class hill climber, `score = hit_rate − α · promote_rate`, sampled atomics | **failed** — −11.5 % to −22.4 % vs default. Bookkeeping cost 14–23 %; score function one-sided. |
| **v4** (round 2) | thread-local + 2-arm bandit (above) | **partial** — within 1σ of default on TPC-C (−0.83 %) and Random Read (−0.23 %); −7.4 % on Skewed YCSB (significant). |

## Failure notes (preserve for future ideation — banlist material)

* **`hit_rate − α · promote_rate` is a one-sided score.** Only penalises promotion; never rewards it when promotion would lift hit_rate. Controller drifts toward "less promote" regardless of where throughput optimum sits.
* **`hit_rate` is monotonic in `log_chance`.** Bandit on hit_rate walks to extremes (lc=1 or lc=14) instead of stopping at the throughput optimum, because the gradient never flips sign at the right point.
* **Per-class atomic counters are too expensive** at 25 M+ TX/s workloads — bookkeeping floor alone exceeds the 1–2 % adaptation benefit ceiling.

## What worked (preserve as positive learning)

* Thread-local counters with bulk-flush eliminated 95 % of the round-1 overhead.
* Counterfactual A/B comparison successfully estimated the gradient *direction* (even if it overshoots).

## Tested by

* exp:E1 (sensitivity sweep — set the baseline)
* exp:E2 (CBP-v3 comparison)
* exp:E3 (CBP-v4 comparison)

## Connections

[AUTO-GENERATED from graph/edges.jsonl]

* inspired_by → paper:zinsmeister2026_predictive_translation
* addresses_gap → gap:G1
* tested_by → exp:E1, exp:E2, exp:E3
