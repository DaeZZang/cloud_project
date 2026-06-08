#ifndef PREDICACHE_CBP_HPP
#define PREDICACHE_CBP_HPP
//
// CBP — Cost-Benefit Promotion (v1..v4 in one file).
//
// Activation:
//   CBP=1  + (default) → v3 controller: 64 per-class hill climbers, 1-in-8 sampling, deadband.
//   CBP=1  + CBP_STATIC=1 → v3 counters wired but no adaptation (overhead-only baseline).
//   CBP_V4=1            → v4 controller: 2-arm bandit (lc_lo, lc_lo+1), thread-local counters
//                         flushed once per epoch (= 1/4096 atomics-per-access amortised).
//                         Pure d(hit_rate)/d(log_chance) signal; no α tunable.
//
// All variants live behind `cbp::enabled`. Falls back to PrediCache's hand-picked masks otherwise.
//
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "buffer_frame.hpp"
#include "page.hpp"
#include "utils/RandomGenerator.hpp"

extern __thread uint16_t workerThreadId;

namespace cbp {

// ─── v3 (per-class hill climber) ───────────────────────────────────────────
constexpr unsigned NUM_CLASSES   = 64;
constexpr unsigned CLASS_MASK    = NUM_CLASSES - 1;
constexpr unsigned WINDOW        = 1u << 12; // 4 K observed accesses per class
constexpr unsigned MIN_LOG       = 1;
constexpr unsigned MAX_LOG       = 14;
constexpr unsigned DEFAULT_LOG   = 5;        // chance = 32
constexpr float    ALPHA         = 0.20f;
constexpr unsigned SAMPLE_MASK   = 0x07u;

struct alignas(64) ClassStats {
    std::atomic<uint32_t> total;
    std::atomic<uint32_t> in_pref;
    std::atomic<uint32_t> promotes;
    std::atomic<uint8_t>  log_chance;
    std::atomic<int8_t>   last_dir;
    std::atomic<uint8_t>  warmup;
    std::atomic<int32_t>  last_score_q;
    char _pad[64 - 15];
};

inline ClassStats classes[NUM_CLASSES] = {};
inline bool       enabled     = false;
inline bool       static_mode = false;
inline bool       v4_mode     = false;
inline bool       tpb_mode    = false;

// ─── TPB (Throughput-Probed Bandit) state ──────────────────────────────────
// Static initializers are placeholders; the real values come from cbp::init()
// driven by env CBP_LOG / TPB_INIT_LC. To avoid confusion, leave these at
// DEFAULT_LOG so a missing init() still yields "matches the static default 1/32".
inline std::atomic<uint8_t> tpb_lc_active{DEFAULT_LOG};
inline uint8_t  tpb_lc_base    = DEFAULT_LOG;   // current "best known" lc
inline uint8_t  tpb_lc_probe   = DEFAULT_LOG;   // lc we're probing this window
inline bool     tpb_probing    = false;         // are we in a probe sandwich?
inline unsigned tpb_window_idx = 0;
inline double   tpb_base_pre   = 0;             // throughput observed at lc_base before probe
inline double   tpb_probe_tps  = 0;             // throughput observed at lc_probe
inline int8_t   tpb_last_dir   = -1;            // alternate probe direction
// EWMA of throughput at lc_base, used as a stability anchor
inline double   tpb_base_ewma  = 0;
// 1 probe per N windows; lower N = more exploration tax, higher N = slower
// adaptation but smaller tax.  Env-configurable so we can sweep.
inline unsigned tpb_probe_every = 8;            // default: probe every 8 windows
// Phase tolerance: how much can the pre and post baselines disagree before we
// veto a probe-sandwich as "phase changed"?  TPC-C grows its dataset 0.8→10 GB
// over a 30 s run; pre/post throughput naturally drift 10-15 % over the 3
// windows of a sandwich.  The 5 % default was too strict for declining
// workloads — it vetoed legitimate probes by mistaking dataset-growth trend
// for a phase change.  Default now 0.15 (15 %), env-configurable via
// TPB_PHASE_TOL (value in percent, e.g. "15" or "100" to effectively disable).
inline double   tpb_phase_tol  = 0.15;
constexpr double   TPB_ACCEPT_EPS  = 0.005;     // 0.5% throughput margin to accept probe

// ─── v4 (2-arm bandit, thread-local accumulators) ─────────────────────────
//
// State (global): two arms with `arm_lc[0] = lc_lo`, `arm_lc[1] = lc_lo + 1`.
// Class assignment: arm = class & 1 (deterministic, no per-access overhead).
// Each thread owns a 4-slot accumulator: per-arm (total, in_pref). Once a thread's
// arm[a].total reaches CBP_V4_FLUSH (= 4096), it bulk-adds to global g_arm[a]
// (one CAS per 4096 accesses ≈ 250 ns / 4096 = 0.06 ns/access, negligible).
//
constexpr unsigned V4_FLUSH = 1u << 12; // bulk-flush interval per arm

struct alignas(64) ArmGlobal {
    std::atomic<uint64_t> total;   // accumulated total observations
    std::atomic<uint64_t> hits;    // accumulated preferred-frame hits
    std::atomic<uint64_t> epoch;   // monotonically increases per flush
    char _pad[64 - 24];
};

inline ArmGlobal g_arm[2] = {};
inline std::atomic<uint8_t> arm_lc[2] = {std::atomic<uint8_t>(4), std::atomic<uint8_t>(5)};
// Adaptation cadence: flip-decision triggered every NUM_DECISION_EPOCHS flushes per arm.
constexpr uint64_t NUM_DECISION_EPOCHS = 8;
inline std::atomic<uint64_t> g_last_decision_epoch{0};

// Per-thread accumulators. We use a thread-local uint32_t[4]:
//   [arm][0] = total observed since last flush, [arm][1] = preferred-frame hits since last flush.
// 256 worker threads × 4 × 4 B = 4 KB total — negligible.
struct alignas(64) ThreadAcc {
    uint32_t arm_total[2] = {0, 0};
    uint32_t arm_hits[2]  = {0, 0};
    char _pad[64 - 16];
};
inline ThreadAcc t_acc[256] = {};

// ─── Public API ───────────────────────────────────────────────────────────

inline unsigned classOf(uint64_t hash) {
    return (hash >> 6) & CLASS_MASK;
}

inline uint64_t maskFor(unsigned cls) {
    if (tpb_mode) {
        uint8_t lc = tpb_lc_active.load(std::memory_order_relaxed);
        return (1u << lc) - 1u;
    }
    if (v4_mode) {
        unsigned arm = cls & 1u;
        uint8_t lc = arm_lc[arm].load(std::memory_order_relaxed);
        return (1u << lc) - 1u;
    }
    return (1u << classes[cls].log_chance.load(std::memory_order_relaxed)) - 1u;
}

inline bool decideForClass(unsigned cls) {
    return (FastRandomGenerator::getRandU64() & maskFor(cls)) == 0;
}

inline void noteAttempt(unsigned cls) {
    if (!v4_mode) classes[cls].promotes.fetch_add(1, std::memory_order_relaxed);
    // v4: we do not need promote count — score is just hit_rate.
}

// v4 path — observe access via thread-local accumulator, flush at CBP_V4_FLUSH.
inline void noteAccessV4(unsigned cls, bool hit) {
    unsigned arm = cls & 1u;
    ThreadAcc& a = t_acc[::workerThreadId];
    uint32_t& tot = a.arm_total[arm];
    uint32_t& hits = a.arm_hits[arm];
    tot++;
    hits += hit ? 1u : 0u;
    if (tot < V4_FLUSH) return;
    // Bulk-flush this thread's arm counters to global.
    g_arm[arm].total.fetch_add(tot, std::memory_order_relaxed);
    g_arm[arm].hits.fetch_add(hits,  std::memory_order_relaxed);
    uint64_t my_epoch = g_arm[arm].epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    tot = 0;
    hits = 0;
    // Adapter check: every NUM_DECISION_EPOCHS new epochs (cumulative across arms).
    uint64_t total_epochs =
        g_arm[0].epoch.load(std::memory_order_relaxed) +
        g_arm[1].epoch.load(std::memory_order_relaxed);
    uint64_t last = g_last_decision_epoch.load(std::memory_order_relaxed);
    if (total_epochs - last < NUM_DECISION_EPOCHS) return;
    if (!g_last_decision_epoch.compare_exchange_strong(last, total_epochs,
                                                       std::memory_order_acq_rel))
        return; // someone else is adapting
    // ─── Decision rule ──
    uint64_t T0 = g_arm[0].total.load(std::memory_order_relaxed);
    uint64_t T1 = g_arm[1].total.load(std::memory_order_relaxed);
    uint64_t H0 = g_arm[0].hits.load(std::memory_order_relaxed);
    uint64_t H1 = g_arm[1].hits.load(std::memory_order_relaxed);
    if (T0 < V4_FLUSH || T1 < V4_FLUSH) return; // need more data
    // Hit rate × access weight = absolute hits — but we want d(hit_rate)/d(lc).
    // arm 0 has lc_lo (more promote), arm 1 has lc_hi=lc_lo+1 (less promote).
    // If arm 0 has higher hit_rate ⇒ promotion still helping ⇒ shift lc_lo down.
    // If arm 1 ≥ arm 0 ⇒ promotion already saturated ⇒ shift lc_lo up.
    double hr0 = static_cast<double>(H0) / static_cast<double>(T0);
    double hr1 = static_cast<double>(H1) / static_cast<double>(T1);
    // Margin: require ≥ 0.5 % gap to act, else hold.
    constexpr double MARGIN = 0.005;
    uint8_t lo = arm_lc[0].load(std::memory_order_relaxed);
    uint8_t hi = arm_lc[1].load(std::memory_order_relaxed);
    if (hr0 - hr1 > MARGIN && lo > MIN_LOG) {
        // arm 0 (more promote) wins ⇒ shift lc_lo down by 1
        arm_lc[0].store(static_cast<uint8_t>(lo - 1), std::memory_order_relaxed);
        arm_lc[1].store(static_cast<uint8_t>(lo),     std::memory_order_relaxed);
    } else if (hr1 - hr0 > MARGIN && hi < MAX_LOG) {
        // arm 1 (less promote) wins ⇒ shift lc_lo up by 1
        arm_lc[0].store(static_cast<uint8_t>(hi),     std::memory_order_relaxed);
        arm_lc[1].store(static_cast<uint8_t>(hi + 1), std::memory_order_relaxed);
    }
    // Reset global counters so the next decision is on fresh data.
    g_arm[0].total.store(0, std::memory_order_relaxed);
    g_arm[1].total.store(0, std::memory_order_relaxed);
    g_arm[0].hits.store(0, std::memory_order_relaxed);
    g_arm[1].hits.store(0, std::memory_order_relaxed);
}

inline void noteAccess(unsigned cls, bool hit) {
    if (tpb_mode) return;                       // ZERO hot-path overhead under TPB
    if (v4_mode) { noteAccessV4(cls, hit); return; }
    // ── v3 path (same as before) ─────────────────────────────────────────
    if ((FastRandomGenerator::getRandU64() & SAMPLE_MASK) != 0) return;
    classes[cls].in_pref.fetch_add(hit ? 1u : 0u, std::memory_order_relaxed);
    uint32_t before = classes[cls].total.fetch_add(1, std::memory_order_relaxed);
    if (before + 1 < WINDOW) return;
    if (static_mode) {
        classes[cls].total.store(0, std::memory_order_relaxed);
        classes[cls].in_pref.store(0, std::memory_order_relaxed);
        classes[cls].promotes.store(0, std::memory_order_relaxed);
        return;
    }
    uint32_t expected = before + 1;
    if (!classes[cls].total.compare_exchange_strong(expected, 0,
                                                    std::memory_order_acq_rel))
        return;
    uint32_t hits = classes[cls].in_pref.exchange(0, std::memory_order_acq_rel);
    uint32_t prom = classes[cls].promotes.exchange(0, std::memory_order_acq_rel);
    uint32_t T    = before + 1;
    float hit_rate  = static_cast<float>(hits) / static_cast<float>(T);
    float prom_rate = static_cast<float>(prom) / static_cast<float>(T);
    float score     = hit_rate - ALPHA * prom_rate;
    int32_t score_q = static_cast<int32_t>(score * 1e6f);
    uint8_t wu = classes[cls].warmup.fetch_add(1, std::memory_order_relaxed);
    int32_t last = classes[cls].last_score_q.exchange(score_q, std::memory_order_relaxed);
    if (wu < 3u) return;
    int8_t  dir = classes[cls].last_dir.load(std::memory_order_relaxed);
    if (dir == 0) dir = ((cls & 1u) ? -1 : +1);
    constexpr int32_t EPS_Q = 5000;
    bool improved = (score_q - last) > EPS_Q;
    bool worsened = (last - score_q) > EPS_Q;
    if (worsened) dir = -dir;
    uint8_t lc = classes[cls].log_chance.load(std::memory_order_relaxed);
    if (improved || worsened) {
        if (dir < 0 && lc > MIN_LOG) lc--;
        else if (dir > 0 && lc < MAX_LOG) lc++;
    }
    classes[cls].log_chance.store(lc, std::memory_order_relaxed);
    classes[cls].last_dir.store(dir, std::memory_order_relaxed);
}

// ─── Init / dump ─────────────────────────────────────────────────────────

// ─── TPB controller: invoked once per "stat window" (default = once per second) ─
// `tx_delta` is the number of transactions completed in this window (already
// captured by the stat thread for stdout logging).
inline void tpbOnWindow(uint64_t tx_delta) {
    if (!tpb_mode) return;
    double tps = static_cast<double>(tx_delta); // per-window count is fine (windows are equal length)
    tpb_window_idx++;
    if (tpb_probing) {
        // We are in window 2 (probe) or 3 (post-baseline) of a 3-window sandwich.
        if (tpb_probe_tps == 0) {
            // window 2: measure probe
            tpb_probe_tps = tps;
            // back to baseline for the post-window
            tpb_lc_active.store(tpb_lc_base, std::memory_order_relaxed);
        } else {
            // window 3: post-baseline; finalize decision
            double base_post = tps;
            double base_pre  = tpb_base_pre;
            double base_avg  = 0.5 * (base_pre + base_post);
            bool phase_stable = base_pre > 0
                && std::abs(base_pre - base_post) / base_pre < tpb_phase_tol;
            if (phase_stable && tpb_probe_tps > base_avg * (1.0 + TPB_ACCEPT_EPS)) {
                // Capture the direction we just moved BEFORE mutating lc_base,
                // otherwise the comparison is always false (probe == base) and
                // last_dir would always collapse to -1.
                int8_t accepted_dir = (tpb_lc_probe > tpb_lc_base) ? +1 : -1;
                tpb_lc_base = tpb_lc_probe;            // adopt the probe
                // Next probe goes the OPPOSITE direction to verify we're at the
                // optimum (sample both sides). If still better the other way,
                // the new probe is accepted and the search continues.
                tpb_last_dir = -accepted_dir;
            }
            tpb_lc_active.store(tpb_lc_base, std::memory_order_relaxed);
            tpb_base_ewma = 0.7 * tpb_base_ewma + 0.3 * base_avg;
            tpb_probing = false;
            tpb_probe_tps = 0;
        }
    } else {
        tpb_base_ewma = 0.7 * tpb_base_ewma + 0.3 * tps;
        if (tpb_window_idx % tpb_probe_every == 0) {
            // Open a probe-sandwich. Save current baseline TPS, switch lc.
            tpb_base_pre = tps;
            // pick probe direction (alternate; clamp at boundaries)
            int new_lc = static_cast<int>(tpb_lc_base) + tpb_last_dir;
            if (new_lc < static_cast<int>(MIN_LOG) || new_lc > static_cast<int>(MAX_LOG)) {
                tpb_last_dir = -tpb_last_dir;
                new_lc = static_cast<int>(tpb_lc_base) + tpb_last_dir;
            }
            tpb_lc_probe = static_cast<uint8_t>(new_lc);
            tpb_lc_active.store(tpb_lc_probe, std::memory_order_relaxed);
            tpb_probing = true;
            tpb_probe_tps = 0;
        } else {
            tpb_lc_active.store(tpb_lc_base, std::memory_order_relaxed);
        }
    }
}

inline void init() {
    enabled = false;
    static_mode = false;
    v4_mode = false;
    tpb_mode = false;

    const char* v = getenv("CBP");
    if (v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y')) enabled = true;
    const char* s = getenv("CBP_STATIC");
    if (s && (s[0] == '1' || s[0] == 'y' || s[0] == 'Y')) { static_mode = true; enabled = true; }
    const char* v4 = getenv("CBP_V4");
    if (v4 && (v4[0] == '1' || v4[0] == 'y' || v4[0] == 'Y')) {
        v4_mode = true; enabled = true;
    }
    const char* t = getenv("TPB");
    if (t && (t[0] == '1' || t[0] == 'y' || t[0] == 'Y')) {
        tpb_mode = true; enabled = true;
    }
    const char* lg = getenv("CBP_LOG");
    unsigned start = DEFAULT_LOG;
    if (lg) {
        unsigned x = static_cast<unsigned>(atoi(lg));
        if (x >= MIN_LOG && x <= MAX_LOG) start = x;
    }
    // TPB-specific override. CBP_LOG affects both v3/v4 and TPB; TPB_INIT_LC
    // affects ONLY TPB. Default policy: TPB starts at the paper's static
    // default (lc=5 / chance=1/32) so it's a fair comparison; the experimenter
    // can opt into a warm start (e.g. TPB_INIT_LC=4) to separate the controller
    // from the convergence cost.
    unsigned tpb_start = start;
    const char* tlc = getenv("TPB_INIT_LC");
    if (tlc) {
        unsigned x = static_cast<unsigned>(atoi(tlc));
        if (x >= MIN_LOG && x <= MAX_LOG) tpb_start = x;
    }
    // Phase-tolerance override: value is in PERCENT (e.g. "15" for 0.15).
    // Use "100" to effectively disable the phase guard.
    tpb_phase_tol = 0.15;
    const char* pt = getenv("TPB_PHASE_TOL");
    if (pt) {
        double x = atof(pt);
        if (x > 0 && x < 100000) tpb_phase_tol = x / 100.0;
    }
    // Probe cadence: 1 probe per N windows.  Lower → more exploration tax,
    // higher → slower adaptation but smaller tax.
    tpb_probe_every = 8;
    const char* pe = getenv("TPB_PROBE_EVERY");
    if (pe) {
        unsigned x = static_cast<unsigned>(atoi(pe));
        if (x >= 2 && x <= 1024) tpb_probe_every = x;
    }

    // v3 state init
    for (unsigned i = 0; i < NUM_CLASSES; ++i) {
        classes[i].total.store(0, std::memory_order_relaxed);
        classes[i].in_pref.store(0, std::memory_order_relaxed);
        classes[i].promotes.store(0, std::memory_order_relaxed);
        classes[i].log_chance.store(static_cast<uint8_t>(start), std::memory_order_relaxed);
        classes[i].last_dir.store(0, std::memory_order_relaxed);
        classes[i].warmup.store(0, std::memory_order_relaxed);
        classes[i].last_score_q.store(0, std::memory_order_relaxed);
    }
    // v4 state init
    for (unsigned a = 0; a < 2; ++a) {
        g_arm[a].total.store(0, std::memory_order_relaxed);
        g_arm[a].hits.store(0, std::memory_order_relaxed);
        g_arm[a].epoch.store(0, std::memory_order_relaxed);
    }
    arm_lc[0].store(static_cast<uint8_t>(start), std::memory_order_relaxed);
    arm_lc[1].store(static_cast<uint8_t>(start + 1), std::memory_order_relaxed);
    g_last_decision_epoch.store(0, std::memory_order_relaxed);
    for (unsigned i = 0; i < 256; ++i) {
        t_acc[i].arm_total[0] = t_acc[i].arm_total[1] = 0;
        t_acc[i].arm_hits[0] = t_acc[i].arm_hits[1] = 0;
    }
    // TPB state
    tpb_lc_base   = static_cast<uint8_t>(tpb_start);
    tpb_lc_probe  = static_cast<uint8_t>(tpb_start);
    tpb_lc_active.store(static_cast<uint8_t>(tpb_start), std::memory_order_relaxed);
    tpb_probing   = false;
    tpb_window_idx = 0;
    tpb_base_pre  = 0;
    tpb_probe_tps = 0;
    tpb_last_dir  = -1;                          // first probe goes downward by default
    tpb_base_ewma = 0;
}

inline void dumpFinal(FILE* f) {
    if (!enabled) return;
    if (tpb_mode) {
        fprintf(f, "tpb,lc_base,lc_active,base_ewma,windows\n");
        fprintf(f, "tpb,%u,%u,%.0f,%u\n",
                (unsigned)tpb_lc_base,
                (unsigned)tpb_lc_active.load(std::memory_order_relaxed),
                tpb_base_ewma,
                tpb_window_idx);
        return;
    }
    if (v4_mode) {
        fprintf(f, "cbp_v4,arm,lc,total,hits\n");
        for (unsigned a = 0; a < 2; ++a) {
            fprintf(f, "%u,%u,%u,%lu,%lu\n", a, a,
                    (unsigned)arm_lc[a].load(std::memory_order_relaxed),
                    (unsigned long)g_arm[a].total.load(std::memory_order_relaxed),
                    (unsigned long)g_arm[a].hits.load(std::memory_order_relaxed));
        }
    } else {
        fprintf(f, "cbp_class,log_chance\n");
        for (unsigned i = 0; i < NUM_CLASSES; ++i)
            fprintf(f, "%u,%u\n", i, (unsigned)classes[i].log_chance.load(std::memory_order_relaxed));
    }
}

} // namespace cbp

#endif // PREDICACHE_CBP_HPP
