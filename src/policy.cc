#include "policy.h"

// ---------------------------------------------------------------------------
// Thresholds
// ---------------------------------------------------------------------------

// L0 file counts at which tiers activate.
// RocksDB's built-in slowdown default is 20 files; we react earlier.
static constexpr uint64_t L0_WARNING_THRESHOLD = 12; // ~60% of default slowdown trigger
static constexpr uint64_t L0_IDLE_THRESHOLD    = 4;  // at or below the compaction trigger

// Compaction backlog at which WARNING activates (~8 GB).
static constexpr uint64_t PENDING_WARNING_BYTES = 8ULL * 1024 * 1024 * 1024;

// ---------------------------------------------------------------------------
// Tier classification
// ---------------------------------------------------------------------------

Tier ClassifyTier(const Metrics& m) {
    // EMERGENCY: RocksDB is actively throttling or halting writes right now.
    if (m.is_write_stopped || m.actual_delayed_write_rate > 0)
        return Tier::EMERGENCY;

    // WARNING: L0 backlog is growing and will cause stalls soon.
    if (m.l0_file_count >= L0_WARNING_THRESHOLD ||
        m.pending_compaction_bytes >= PENDING_WARNING_BYTES)
        return Tier::WARNING;

    // IDLE: very light load, no compaction pressure.
    if (m.l0_file_count <= L0_IDLE_THRESHOLD &&
        m.num_running_compactions == 0 &&
        !m.memtable_flush_pending)
        return Tier::IDLE;

    return Tier::NOMINAL;
}

// ---------------------------------------------------------------------------
// Tier → Action mapping
//
// Each tier returns the *full* desired configuration so that settings always
// converge to the correct state, regardless of what tier we came from.
//
// DB-level knob:
//   max_background_jobs — total background threads shared by compaction + flush.
//
// CF-level knobs:
//   level0_slowdown_writes_trigger — L0 file count at which writes are slowed.
//   level0_stop_writes_trigger     — L0 file count at which writes are stopped.
//
// In EMERGENCY we also raise the stall thresholds to give compaction room to
// drain the backlog before writes are stopped again.
// ---------------------------------------------------------------------------

Action TierToAction(Tier tier) {
    Action a;
    switch (tier) {
        case Tier::EMERGENCY:
            a.db_options["max_background_jobs"]             = "6";
            a.cf_options["level0_slowdown_writes_trigger"]  = "32";
            a.cf_options["level0_stop_writes_trigger"]      = "56";
            break;

        case Tier::WARNING:
            a.db_options["max_background_jobs"]             = "4";
            a.cf_options["level0_slowdown_writes_trigger"]  = "24";
            a.cf_options["level0_stop_writes_trigger"]      = "40";
            break;

        case Tier::NOMINAL:
            a.db_options["max_background_jobs"]             = "3";
            a.cf_options["level0_slowdown_writes_trigger"]  = "20";
            a.cf_options["level0_stop_writes_trigger"]      = "36";
            break;

        case Tier::IDLE:
            a.db_options["max_background_jobs"]             = "2";
            a.cf_options["level0_slowdown_writes_trigger"]  = "20";
            a.cf_options["level0_stop_writes_trigger"]      = "36";
            break;
    }
    return a;
}

// ---------------------------------------------------------------------------

const char* TierName(Tier tier) {
    switch (tier) {
        case Tier::EMERGENCY: return "EMERGENCY";
        case Tier::WARNING:   return "WARNING";
        case Tier::NOMINAL:   return "NOMINAL";
        case Tier::IDLE:      return "IDLE";
    }
    return "UNKNOWN";
}
