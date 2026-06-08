---
type: paper
node_id: paper:zinsmeister2026_predictive_translation
title: "Predictive Translation: High-Performance Buffer Management Without the Trade-Offs"
authors: ["Michael Zinsmeister", "Lam-Duy Nguyen", "Viktor Leis", "Thomas Neumann"]
year: 2026
venue: "SIGMOD"
external_ids:
  arxiv: null
  doi: "10.1145/3786678"
  s2: null
tags: ["buffer-management", "DBMS", "hash-table", "superscalar", "SSD-optimized", "promotion-policy"]
added: 2026-05-11T00:01:00Z
relevance: core
---

# Predictive Translation: High-Performance Buffer Management Without the Trade-Offs

## One-line thesis

A chaining-hash-table buffer pool can match the performance of vmcache and pointer-swizzling designs by giving every PID a deterministic preferred frame address, allowing CPU superscalar execution to overlap the hash lookup with a speculative read from the predicted frame.

## Problem / Gap

Existing high-performance buffer pools (pointer swizzling, LIPAH, vmcache) sacrifice qualitative properties: pointer swizzling forbids cyclic references, LIPAH is capped at 17 TB, vmcache needs a kernel module (`exmap`) and pays massive metadata overhead. The paper challenges the "hash tables are too slow" assumption that drives these designs.

## Method

* **Predictive translation:** every PID maps to a *preferred buffer frame* via `MurmurHash2(pid) mod frameCount`. CPU speculatively reads that frame while the hash-table lookup is in flight; superscalar execution hides one of the two memory accesses.
* **Promotion lifecycle:** new pages land in arbitrary frames (one-hit-wonder hypothesis); on the *second* access a page is probabilistically promoted to its preferred frame with chance `1/32`, or `1/512` if it must displace an existing page.
* **Lightweight HT:** chaining hash table sized at 2× pool, first slot inlined, synchronized by optimistic versioned latches.

## Key Results

* TPC-C in-memory @ 192 t: +34 % over vmcache, +19 % over LeanStore.
* OOM TPC-C @ 192 t: +19.7 % / +24.6 % over vmcache / LeanStore.
* Single-threaded random read: 3 510 cycles/op vs 3 939 (vmcache), 5 090 (Traditional).
* 91 % of accesses hit the preferred frame even at 3× OOM dataset.
* Hash chain length distribution: 97 % of chains ≤ 3 entries at fill factor 1.

## Assumptions

* Workload has hot/cold separation so the "second access promotes to preferred frame" lifecycle works.
* Modern CPUs are superscalar enough to overlap two independent memory accesses.
* MurmurHash2 distributes PIDs sufficiently uniformly.

## Limitations / Failure Modes

* **Random uniform reads** are the worst case — preferred-frame hits stay high (91 %) but the speculation gives no benefit since all leaves are equally hot/cold.
* **Promotion probabilities (1/32, 1/512) are hand-picked constants** with no workload-adaptive mechanism.
* **Variable-sized pages are out of scope** (mentioned as future work).
* Promotion involves a 4 KB memcpy — amortized but expensive per event.
* Out-of-memory experiments need fast NVMe; Lustre is uninformative.

## Reusable Ingredients

* Optimistic versioned latch as a lock-free synchronization primitive.
* `getPreferredFrame(pid)` as a generic "deterministic placement" trick — adaptable to any context where speculative access could hide an indirection.
* The fast-path/slow-path branching pattern (line 50, `guards.hpp`) as a template for letting branch prediction + speculation hide hash-table cost.

## Open Questions

* Can the hand-picked 1/32 / 1/512 be replaced by adaptation? **Tested as idea:CBP — see below.**
* How does this scale to variable-sized pages (Bf-Tree-style tiers)?
* Cloud-native cost model for promotion in a paged-memory environment.

## Claims

* claim:C1 — On TPC-C and random-read, the optimum static promotion chance is 1/16, not 1/32.
* claim:C2 — Per-class atomic-counter bookkeeping costs 8–23 % throughput at 16 threads.
* claim:C4 — A 2-arm counterfactual bandit on hit_rate breaks even with 1/32 on 2/3 workloads.

## Connections

[AUTO-GENERATED from graph/edges.jsonl — see edges file for canonical relationships]

* extends → paper:leis2023_vmcache (challenges its claim that hash tables are too slow)
* extends → paper:leis2018_leanstore (alternative to pointer swizzling)
* extended_by → idea:CBP (replaces 1/32 with adaptation)

## Relevance to This Project

The paper IS this project. We reproduced its prototype and built an extension (CBP) addressing one of its open questions.

## Abstract (original)

> To efficiently manage larger-than-memory datasets, storage-based database management systems (DBMSs) rely on buffer managers. These are traditionally implemented using hash tables to translate page identifiers (PIDs) to memory pointers. While this design offers many practical advantages, prior studies have shown its performance limitations and proposed alternative designs to close the gap with optimized in-memory DBMSs. However, these modern designs introduce systematic issues, such as intrusive implementations or reliance on kernel modules, which ultimately hinder their adoption. This paper challenges the notion that hash-table-based buffer pools cannot deliver high performance. We introduce predictive translation, a novel approach that combines the high performance of modern approaches with the qualitative benefits of traditional designs. Predictive translation achieves this by exploiting the capabilities of commodity CPUs – particularly their superscalar execution – through deterministic placement of pages to hide the excessive latency of software-level hash table lookups. Our evaluation demonstrates that our approach meets all practical requirements while delivering performance at least on par with state-of-the-art alternatives. We show that our design is a compelling solution for buffer management in modern DBMSs running on fast storage devices.
