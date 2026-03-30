#pragma once

#include <cstdint>

namespace rocksdb { class DB; }

// A point-in-time snapshot of the DB state relevant to compaction control.
// All fields are read via DB::GetIntProperty(), which reflects current live
// state rather than cumulative counters.
struct Metrics {
    uint64_t l0_file_count;            // current number of L0 SST files
    uint64_t pending_compaction_bytes; // estimated bytes waiting to be compacted
    uint64_t actual_delayed_write_rate;// imposed write rate limit (0 = no stall)
    bool     is_write_stopped;         // true if writes are completely halted
    uint64_t num_running_compactions;  // compaction threads active right now
    uint64_t num_running_flushes;      // flush threads active right now
    bool     memtable_flush_pending;   // true if a memtable is queued to flush
};

// Read the current DB state into a Metrics snapshot.
Metrics CollectMetrics(rocksdb::DB* db);
