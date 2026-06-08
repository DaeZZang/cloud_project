---
type: paper
node_id: paper:anonymous2025_adaptiveclimb
title: "Adaptive Cache Replacement with Dynamic Resizing (DynamicAdaptiveClimb)"
authors: ["(anonymous arXiv submission)"]
year: 2025
venue: "arXiv 2511.21235"
external_ids:
  arxiv: "2511.21235"
  doi: null
  s2: null
tags: ["cache-replacement", "adaptive", "LRU", "novelty-check"]
added: 2026-05-11T00:01:30Z
relevance: peripheral
---

# Adaptive Cache Replacement with Dynamic Resizing (DynamicAdaptiveClimb)

## One-line thesis

Adapts the promotion *distance* of a CLIMB-style cache list (LRU variant) based on recent hit/miss ratios, using a single tunable parameter and no per-item statistics.

## Problem / Gap

LRU and CLIMB are static; their promotion behavior doesn't react to workload phase changes.

## Method

* Maintain a CLIMB-style list ordering.
* Periodically observe hit-rate over a window.
* Adjust the "jump" distance up or down based on whether the hit-rate trend is improving.
* DynamicAdaptiveClimb also resizes the cache.

## Key Results

Up to 29 % hit-ratio improvement over baselines on 1067 real-world traces.

## Limitations / Failure Modes

* CLIMB list semantics — applies to *generic web/disk cache replacement*, not buffer-pool *frame placement*.
* Does not address the deterministic-preferred-frame question that PrediCache faces.

## Reusable Ingredients

* The "single-parameter adaptive policy with no per-item state" pattern is closest in spirit to CBP, but operates at the list-position level, not the placement level.

## Open Questions

(none relevant to our project)

## Claims

(none)

## Connections

[AUTO-GENERATED from graph/edges.jsonl]

* (only used as novelty-check evidence — distinct problem, distinct mechanism)

## Relevance to This Project

Used in the novelty check for CBP. Confirmed CBP is **not** a re-discovery of AdaptiveClimb — AdaptiveClimb adjusts LRU-list promotion *distance* in generic cache replacement, while CBP adjusts probabilistic *promotion-to-preferred-frame* probability in a DBMS buffer pool. Same word ("promotion"), different mechanisms.
