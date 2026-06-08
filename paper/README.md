# TPB Paper

**File:** `tpb.tex` → compiles to `tpb.pdf` (5 pages, ~97 KB)
**Build:** `tectonic tpb.tex` (no system TeX install required — tectonic
downloads what it needs on first run; subsequent builds use cache).

**Target venue:** DaMoN (Workshop on Data Management on New Hardware,
co-located with SIGMOD). Workshop format, 4-6 pages typical.

## Sections

| # | Section | Pages |
|---|---|---|
| 1 | Introduction | 1.0 |
| 2 | Background: PrediCache and Promotion | 0.4 |
| 3 | Sensitivity Analysis ($\tfrac{1}{32}$ is workload-dependent) | 0.4 |
| 4 | TPB: Throughput-Probed Bandit (design + mechanism + implementation) | 1.4 |
| 5 | Evaluation (hardware, three-workload results, dev-arc lessons, knob sensitivity) | 1.3 |
| 6 | Related Work | 0.2 |
| 7 | Limitations and Future Work | 0.3 |
| 8 | Conclusion | 0.2 |
| – | References (10 entries) | 1.0 |

## Three concrete claims

1. **TPB matches the static $\tfrac{1}{32}$ default** within one $\sigma$ on TPC-C ($\Delta = -1.29\%$), Random Read ($\Delta = +1.69\%$), and Skewed YCSB ($\Delta = +2.03\%$). $n=5$, 30 s, 16 threads.
2. **TPB eliminates the catastrophic 22.4 % regression** on Skewed YCSB that earlier hot-path-instrumented controllers suffered (CBP-v3 in our dev arc).
3. **Architectural lesson:** instrument outside the hot path, at the system's existing coarsest telemetry boundary — generalisable beyond this specific controller.

## How to reproduce

The paper's results are reproducible from the artifacts in this repo:

```bash
# Sensitivity sweep — Table 1
bash experiments/run_main_v2.sh

# Phase-tol × probe-rate grid — Table 3
bash experiments/run_tpb_v4.sh

# Headline 3-workload result — Table 2
bash experiments/run_tpb_v5_3wl.sh
```

Each script writes per-replicate CSVs under `results/<sweep_name>/`.

## Source / artifact pointers

| Artifact | Path |
|---|---|
| Controller source | `repo/PrediCache/cbp.hpp` (50 LOC, the `tpb::` namespace) |
| Stat-thread hook | `repo/PrediCache/benchmarks/Benchmark.hpp` (1-line addition) |
| Sensitivity raw data | `results/main_v2/` |
| Knob-grid raw data | `results/tpb_v4/` |
| Headline raw data | `results/tpb_v5_3wl/` |

## Build cache

After the first `tectonic` build, downloaded TeX packages are cached at
`~/.cache/Tectonic/` (default).
