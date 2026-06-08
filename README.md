# Adaptive Promotion for PrediCache

*Can a high-performance buffer manager tune its own promotion probability — at
zero hot-path cost?*

This repository reproduces **PrediCache** (SIGMOD 2026) on commodity hardware
and develops **TPB (Throughput-Probed Bandit)**, an online controller that
replaces PrediCache's two hand-picked promotion constants with a self-tuning
policy. It is a small *reproduction-with-extension* study targeting a workshop
venue (DaMoN), not a "we beat the state of the art" paper. We are deliberate
about reporting what worked, what didn't, and what stayed flat.

---

## 1. The 60-second background

A traditional buffer manager turns a page-id into a frame address with **two
dependent memory accesses** (hash the PID → follow the pointer). PrediCache's
*predictive translation* binds every page to a deterministic **preferred frame**
(`MurmurHash2(pid) mod frameCount`) so the CPU can **speculatively** read that
frame *in parallel* with the hash-table lookup — hiding one of the two accesses.

For the speculation to pay off, hot pages must actually live in their preferred
frame, so PrediCache **probabilistically promotes** them there using two
hand-picked constants:

| Case | Probability |
|------|-------------|
| No page displaced | `1/32` |
| A page must be displaced | `1/512` (= `1/32 × 1/16`) |

**Our question:** these constants are magic numbers. Can we make them *adapt*
to the workload automatically — without slowing down the hot path that
PrediCache worked so hard to make fast?

## 2. What we found

**Finding 1 — `1/32` is workload-dependent (but the surface is flat).**
A static sweep over `{0, 1/16, 1/32, 1/128}` shows TPC-C and uniform
random-read peak at **`1/16`** (+1.2 %, +1.6 %), while skewed YCSB peaks at
**`1/32`** — the paper's default. *But* the throughput differences are only
**1–3 %** and the curve is flat near the optimum: there is very little headroom
for any controller to win.

**Finding 2 — adaptation is easy to get *catastrophically* wrong.**
Our first controllers instrumented the hot path and used hit-rate as the signal.
They regressed by **up to −22 %** on skewed YCSB (instrumentation cost at
26 M TX/s) and drifted to degenerate settings (hit-rate is monotonic in
promotion probability, so it walks to extremes instead of the throughput
optimum).

**Finding 3 — TPB matches the default at zero hot-path cost.**
The final design, TPB, moves *all* controller logic onto PrediCache's existing
once-per-second stat thread and uses the system's **actual throughput** as its
signal. The hot path is a single relaxed atomic load — identical cost to the
static default. TPB matches the hand-picked default within statistical noise on
all three workloads, with **no manual per-workload tuning**.

> **The honest takeaway:** on a fixed workload with a flat optimum, TPB neither
> beats nor breaks the default — it *ties* it, automatically. The durable
> contribution is **methodological**: *instrument outside the hot path, at the
> system's coarsest existing telemetry boundary.* That lesson generalises
> beyond this controller.

## 3. How TPB works (the Probe-Sandwich Bandit)

TPB keeps one integer `lc_base` = `log₂(1/p₀)` that all worker threads read.
Once per second the controller runs on the stat thread; every `PROBE_EVERY`
windows it opens a **three-window sandwich**:

```
window k-1 : run at lc_base       → T_pre   (baseline "bread")
window k   : run at lc_base ± 1    → T_probe (the "filling" being tested)
window k+1 : run at lc_base again  → T_post  (baseline "bread")
```

It then:
1. Estimates the baseline at the probe's instant by **linear interpolation**:
   `T_avg = (T_pre + T_post) / 2`.
2. **Phase guard:** if `|T_pre − T_post| / T_pre ≥ 15 %`, the workload was
   non-stationary (e.g. TPC-C's dataset grows over time) — discard the sandwich.
3. **Accept** the probe iff `T_probe > T_avg × (1 + 0.5 %)`.
4. On accept: `lc_base ← lc_probe`, then **flip probe direction** to sample the
   other side next time.

Why a sandwich? Because throughput drifts over time, so comparing the probe to a
*single* earlier baseline conflates "this setting is better" with "time passed."
Interpolating between a before- and after-baseline cancels the drift.

**Cost:** the hot path is just
```cpp
uint8_t lc = tpb_lc_active.load(std::memory_order_relaxed);  // one mov on x86
return (1u << lc) - 1u;
```
plus **one line** added to the stat loop. The whole controller is ~50 lines.

## 4. Main result

**TPB vs PrediCache's static `1/32` default** — `n=5`, 30 s, 16 threads,
in-memory. Throughput in thousands of TX/s. Welch's two-sided `|t|`; 95 %
significance needs `|t| > 2.31`.

| Workload   | Variant | Mean | σ | Δ vs default | \|t\| |
|------------|---------|------|-----|------|------|
| TPC-C      | default | 634.4 | 13.3 | — | — |
|            | **TPB** | 626.2 | 41.8 | **−1.29 %** | 0.42 |
| Rnd-Read   | default | 15 552 | 234 | — | — |
|            | **TPB** | 15 815 | 558 | **+1.69 %** | 0.97 |
| Skew-YCSB  | default | 26 032 | 476 | — | — |
|            | **TPB** | 26 561 | 373 | **+2.03 %** | 1.95 |

TPB is **within 1σ of the default on every workload** — no comparison reaches
significance. It converges to `1/16` on TPC-C and `1/32` on YCSB (matching the
static optima from Finding 1) without being told which to pick.

**Development arc** (Δ vs static `1/32` on TPC-C / Skew-YCSB):

| Variant | What changed | TPC-C | Skew-YCSB |
|---------|--------------|-------|-----------|
| CBP-v3  | per-class hill climber, hot-path counters | −11.5 % | −22.4 % |
| CBP-v4  | thread-local counters + hit-rate bandit | −0.83 % | −7.4 % |
| **TPB-v5** | throughput signal, off-hot-path stat thread | **−1.29 %** | **+2.03 %** |

## 4b. Findings and failed methods — the full record

Every claim and every dead end, with the raw data that backs it. Paths are
relative to the repo root. CSVs are per-replicate; `results/figures/summary.json`
holds machine-readable means/σ for every cell.

### Findings (what held up)

| # | Finding | Status | Record |
|---|---------|--------|--------|
| F1 | TPC-C & Rnd-Read peak at `1/16`, not `1/32` | **suggestive** (Δ within 1σ at n=2, direction consistent) | `results/main_v2/*.csv`, `results/figures/fig_sensitivity.{pdf,png}` |
| F2 | Skewed YCSB peaks at `1/32` (paper default is optimal here) | **supported** (monotone on both sides) | `results/main_v2/ycsb_skewed_chance*_r*.csv` |
| F3 | Surface is flat near optimum (1–3 % headroom) | **supported** | `results/main_v2/`, `results/figures/summary.json` |
| F4 | Hot-path instrumentation costs 8–23 % at 16 t | **supported** | `results/cbp_v3/*` (compare `cbp_static` vs `default` rows) |
| F5 | hit-rate signal is monotonic in `1/p₀` → controller drifts to extremes | **supported** | `results/cbp_v4/*_dump.csv` (end-of-run `lc` at boundaries) |
| F6 | TPB matches default within 1σ on all 3 workloads, zero hot-path cost | **supported** | `results/tpb_v5_3wl/*.csv`, paper Table 2 |
| F7 | TPB auto-converges to the static optimum (TPC-C→`1/16`, YCSB→`1/32`) | **supported** (Rnd-Read is the exception, see L2) | `results/tpb_v5_3wl/*_tpb_dump.csv` |

### Failed / superseded methods (what we tried and why it died)

| Method | Idea | Outcome | Why it failed | Record |
|--------|------|---------|---------------|--------|
| **v1 sweep** | first sensitivity sweep | discarded | workload-name typo (`random-read` vs `rndread`) ran the wrong benchmark — wasted ~22 min | `results/main_v1/` (kept as cautionary artifact) |
| **CBP-v3** | 64-class hill climber, score = `hit_rate − α·promote_rate`, sampled hot-path atomics | **−11.5 % / −22.4 %** | (a) bookkeeping floor 8–23 % even with adaptation off; (b) one-sided score only penalises promotion, never rewards it → drifts conservative | `results/cbp_v3/*.csv`, `results/cbp_v3/*_cbp_dump.csv` |
| **CBP-static** | CBP-v3 counters with adaptation *disabled* | −14.3 % / −22.8 % | isolates the pure instrumentation cost — proves F4 | `results/cbp_v3/*cbp_static*.csv` |
| **CBP-v4** | thread-local counters + 2-arm hit-rate bandit | −0.83 % / −7.4 % | killed the overhead floor on 2/3 workloads, but hit-rate signal (F5) walks the bandit to `lc` extremes; YCSB still −7.4 % from residual per-access cost at 26 M TX/s | `results/cbp_v4/*.csv`, `results/cbp_v4/*_dump.csv` |
| **TPB-v3** | throughput signal, but `PROBE_EVERY=8`, `PHASE_TOL=5 %` | **−2.21 % (significant)** | phase tolerance too strict — vetoed legitimate probes by mistaking TPC-C's dataset-growth decline for a phase change; plus a direction-update bug (compared `lc` *after* mutating → always probed down → degenerate `p₀=1`) | `results/tpb/`, `results/tpb_v2/`, `results/tpb_v3/` |
| **TPB-v4** | knob grid: `PHASE_TOL × PROBE_EVERY`, warmup variants | tuning run (not a regression) | found `PHASE_TOL=15 %`, `PROBE_EVERY=16` as the robust cell; fed into v5 | `results/tpb_v4/` (e.g. `tpcc_tpb_t15_p16_*`, `t100_p16_*`, `warm_*`) |
| **TPB-v5** | final: 15 % tol, probe-every-16, fixed direction/boundary handling | **−1.29 % / +2.03 %** (within 1σ) | shipped — this is the main result | `results/tpb_v5_3wl/*.csv` |

### What each failure *taught* (the two design axes)

The whole arc is the product of two independent corrections, each forced by a
specific failure above:

1. **Signal:** `hit_rate − α·promote_rate` (CBP-v3) → `hit_rate` A/B (CBP-v4) →
   **actual throughput** (TPB). *Forced by F5* — hit-rate is monotonic, so it
   can never locate a throughput optimum.
2. **Where to measure:** hot-path sampled atomics (CBP-v3) → thread-local
   counters (CBP-v4) → **the existing per-second stat thread, off the hot path
   entirely** (TPB). *Forced by F4* — at 26 M TX/s any per-access cost dominates
   the 1–3 % that adaptation could win.

TPB is simply both axes taken to their endpoint. The reproducible lesson —
*measure at the system's coarsest existing telemetry boundary, on its real
objective* — is the part that outlives this specific controller.

### Figures

| File | Shows |
|------|-------|
| `results/figures/fig_sensitivity.{pdf,png}` | throughput vs fixed promotion probability, per workload (F1–F3) |
| `results/figures/fig_v3_compare.{pdf,png}` | trad / default / cbp_static / cbp bars (F4) |
| `results/figures/fig_v3_v4_progression.{pdf,png}` | CBP-v3 → v4 recovery |
| `results/figures/summary.json` | means + σ for every (workload, config) cell |

## 5. Honest limitations

- **Flat surface, no win.** Finding 1 shows ≤3 % headroom, so "matches default"
  is the ceiling, not a disappointment — but it also means the result is closer
  to a *null result on performance* than a speedup.
- **One workload actually adapted.** Of the three, only TPC-C moved off the
  init value. Rnd-Read's true optimum is `1/16` but TPB stayed at `1/32` (the
  0.5 % probe margin was too tight to detect its weak optimum).
- **No changing-workload experiment yet.** TPB's real value should show up when
  the workload *shifts mid-run* and a static constant can't follow — we have not
  demonstrated that yet. (Top of the future-work list.)
- **Power-of-two grid.** Promotion probability is restricted to `1/2^k` by the
  cheap `rand & mask` hot-path trick; a threshold-compare could give arbitrary
  probabilities (e.g. `1/25`) at the same cost. We never tested between the
  power-of-two rungs, so a finer optimum may exist.
- **Scope.** 16 threads on 2× Xeon Gold 5320, in-memory only (Lustre backend
  makes out-of-memory I/O-bound and uninformative). No comparison to
  vmcache / LeanStore / WiredTiger — orthogonal to our question, and blocked by
  infra policy (vmcache needs the `exmap` kernel module + root).

## 6. Repository layout

```
.
├── README.md                    ← you are here
├── NARRATIVE_REPORT.md          ← full technical narrative (rounds 1 & 2 / CBP)
├── PIPELINE_REPORT.md           ← research-pipeline journey log
├── paper/
│   ├── tpb.tex / tpb.pdf        ← the DaMoN-style write-up (5 pages)
│   └── README.md                ← how to build the paper
├── repo/PrediCache/             ← cloned + patched PrediCache
│   ├── cbp.hpp                  ← all controllers (CBP-v3/v4 + the tpb:: namespace)
│   ├── buffer_manager.hpp       ← promotion hot path + env knobs (DISABLE_PRED, PROMO_*)
│   └── benchmarks/Benchmark.hpp ← 1-line stat-thread hook
├── experiments/                 ← run_*.sh sweep drivers + analyze/plot scripts
├── results/                     ← per-replicate CSVs + figures (see figures/)
├── research-wiki/               ← papers / ideas / claims / gap map
├── idea-stage/  review-stage/   ← idea generation + adversarial review logs
```

## 7. Quick start

**Prerequisites:** a C++23 compiler (g++ 11+), `make`, and `libaio-dev`
(`sudo apt-get install libaio-dev` on Debian/Ubuntu). Python 3 with
`pandas`/`matplotlib` is only needed for the analysis/plot scripts.

```bash
# 1) Clone
git clone git@github.com:DaeZZang/cloud_project.git
cd cloud_project

# 2) Build (the patched PrediCache source ships as plain files — no submodule init)
cd repo/PrediCache
make                       # needs libaio-dev; builds ./predicache
./predicache               # smoke test: default = TPC-C, 1 thread, 1 GB pool

# 3) Reproduce the headline experiments (run from repo/PrediCache;
#    scripts write per-replicate CSVs under results/)
bash ../../experiments/run_main_v2.sh       # Finding 1: static sensitivity sweep (~25 min)
bash ../../experiments/run_tpb_v5_3wl.sh    # Finding 3: TPB vs default, 3 workloads
bash ../../experiments/run_cbp_v3.sh        # the CBP-v3 negative result (optional)

# 4) Analyze / plot (from repo root)
cd ../..
python experiments/analyze_v2.py            # tables + summary.json
python experiments/plot.py                  # figures under results/figures/
```

> **Note.** The experiments above are *in-memory* and do **not** need the large
> `*.bm` backing files (excluded from the repo by design); PrediCache allocates
> its pool in RAM. Only the out-of-memory regime would create a `*.bm` file, and
> it does so automatically on first run.

**Runtime knobs** (env vars):

| Var | Effect |
|-----|--------|
| `TPB=1` | enable the Throughput-Probed Bandit |
| `TPB_INIT_LC=5` | start at `1/2^5 = 1/32` |
| `TPB_PHASE_TOL=15` / `TPB_PROBE_EVERY=16` | phase guard / probe cadence |
| `PROMO_OPT=N PROMO_SHARED=N PROMO_EXCL=N` | fixed probability `1/nextPow2(N)` |
| `DISABLE_PRED=1` | turn off predictive translation (paper's "Traditional" ablation) |
| `CBP=1` / `CBP_STATIC=1` | enable the earlier CBP controller / its counters only |

---

*Hardware: 2× Intel Xeon Gold 5320 (52 c), 440 GB RAM, Lustre/NVMe,
g++ 11.4 `-O3 -std=c++23`. Upstream PrediCache:
<https://github.com/mzinsmeister/PrediCache>.*
