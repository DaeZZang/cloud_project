---
type: paper
node_id: paper:leis2023_vmcache
title: "Virtual-Memory Assisted Buffer Management"
authors: ["Viktor Leis", "Adnan Alhomssi", "Tobias Ziegler", "Yannick Loeck", "Christian Dietrich"]
year: 2023
venue: "SIGMOD"
external_ids:
  arxiv: null
  doi: "10.1145/3588928"
  s2: null
tags: ["buffer-management", "DBMS", "virtual-memory", "kernel-module", "SSD-optimized"]
added: 2026-05-11T00:01:30Z
relevance: related
---

# Virtual-Memory Assisted Buffer Management

## One-line thesis

Use the OS page table (with a custom `exmap` kernel module) as the PID-to-frame translation, avoiding software hash-table cost by reusing the TLB and hardware page-walk units.

## Problem / Gap

Hash-table-based buffer pools are claimed to be too slow for in-memory speeds.

## Method

`vmcache` leverages OS virtual memory: each PID is mapped via a kernel-module-managed page table; the TLB gives O(1) translation in hardware. The `exmap` kernel module is required for performant out-of-memory behavior.

## Key Results

In the paper itself: competitive with pointer swizzling. As cited by PrediCache (2026): vmcache loses by 34 % on TPC-C at 192 threads because of (a) `exmap` syscall overhead on new-page allocation, (b) cache-line ping-pong on the page-state array, (c) inability to use huge pages.

## Assumptions

* Root access to install kernel module.
* Working buffer pool size known at allocation time (metadata is proportional to *storage* size, not memory).

## Limitations / Failure Modes

* **Kernel module required** — `exmap` is not upstream; security policy on shared infrastructure typically forbids it.
* **Metadata overhead grows with storage size**: 30 TB SSD ≈ 480 GB of buffer-pool metadata at 64 B/frame-header.
* **Can't use 2 MB huge pages** because the page-size must align with OS VM.
* **Sampling-based eviction (WATT, HyperBolic) hard to integrate** due to metadata constraint.

## Reusable Ingredients

* The idea of "let hardware do the translation" — even when the specific OS-modification path isn't viable, it motivates the predictive-translation alternative.

## Open Questions

* eBPF-based alternative to `exmap` — security-clean but functionally restricted.

## Claims

(none reused in our reproduction; we did not run vmcache)

## Connections

[AUTO-GENERATED from graph/edges.jsonl]

* extended_by → paper:zinsmeister2026_predictive_translation (PrediCache positions itself as a "no kernel module" alternative)

## Relevance to This Project

Cited as one of the three baselines PrediCache compares against. We could not reproduce its measurements (no kernel-module access on shared infrastructure), so we rely on the PrediCache paper's reported numbers for cross-reference.
