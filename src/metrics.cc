#include "metrics.h"

#include "rocksdb/db.h"

// Helper: read a single integer property; returns 0 on failure.
static uint64_t GetProp(rocksdb::DB* db, const std::string& prop) {
    uint64_t val = 0;
    db->GetIntProperty(prop, &val);
    return val;
}

Metrics CollectMetrics(rocksdb::DB* db) {
    using P = rocksdb::DB::Properties;
    Metrics m{};

    m.l0_file_count             = GetProp(db, P::kNumFilesAtLevelPrefix + "0");
    m.pending_compaction_bytes  = GetProp(db, P::kEstimatePendingCompactionBytes);
    m.actual_delayed_write_rate = GetProp(db, P::kActualDelayedWriteRate);
    m.is_write_stopped          = GetProp(db, P::kIsWriteStopped) != 0;
    m.num_running_compactions   = GetProp(db, P::kNumRunningCompactions);
    m.num_running_flushes       = GetProp(db, P::kNumRunningFlushes);
    m.memtable_flush_pending    = GetProp(db, P::kMemTableFlushPending) != 0;

    return m;
}
