---
type: paper
node_id: paper:leis2018_leanstore
title: "LeanStore: In-Memory Data Management beyond Main Memory"
authors: ["Viktor Leis", "Michael Haubenschild", "Alfons Kemper", "Thomas Neumann"]
year: 2018
venue: "ICDE"
external_ids:
  arxiv: null
  doi: "10.1109/ICDE.2018.00026"
  s2: null
tags: ["buffer-management", "DBMS", "pointer-swizzling", "SSD-optimized"]
added: 2026-05-11T00:01:30Z
relevance: related
---

# LeanStore: In-Memory Data Management beyond Main Memory

## One-line thesis

Pointer swizzling — replacing PIDs in index nodes with direct frame pointers — lets a buffer pool match in-memory performance when the working set fits.

## Problem / Gap

Hash-table translation is the dominant cost in disk-based DBMSs serving in-memory-sized data.

## Method

* Each index node stores either a PID (page not loaded) or a "swizzled" memory pointer (page is loaded).
* Page eviction de-swizzles, replacing the pointer with the PID and exclusively latching both the page and its parent.
* Background page-provider threads handle eviction.

## Key Results

* Matches pure in-memory performance when working set fits.
* PrediCache (2026) reports: on TPC-C at 192 t, LeanStore is 19 % slower than PrediCache; on OOM TPC-C, 24.6 % slower.

## Assumptions

* Each page is referenced from exactly **one** location (no cyclic references, no B+-tree sibling links that count).
* Eviction can afford to latch both the page and its parent.

## Limitations / Failure Modes

* **Single-reference rule** — incompatible with graph data structures, B+ tree leaf-sibling pointers, etc.
* **Eviction latches the parent** — adds contention in deep trees.
* Inflexible B+-tree layout (PrediCache exploits this gap with a more compact tree).

## Reusable Ingredients

* The notion that translation cost can be reduced to a single dereference is the central inspiration for predictive translation (which achieves the same property without the swizzling constraint).

## Open Questions

* Can pointer swizzling be relaxed to allow multiple references?

## Claims

(none reused in our reproduction)

## Connections

[AUTO-GENERATED from graph/edges.jsonl]

* extended_by → paper:zinsmeister2026_predictive_translation (PrediCache replaces swizzling with deterministic placement + speculation)

## Relevance to This Project

Second of three baselines in PrediCache's evaluation. We did not run LeanStore (1–3 days of integration work outside the scope of our reproduction).
