#include "compaction_controller.h"

#include "metrics.h"
#include "policy.h"

#include "rocksdb/db.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

// Polling interval between observe-decide-act cycles.
static constexpr int kLoopIntervalSeconds = 5;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void Log(const char* fmt, ...) {
    time_t t = time(nullptr);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);
    fprintf(stderr, "[ClearCtrl %s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void ApplyAction(rocksdb::DB* db, const Action& a) {
    if (!a.db_options.empty()) {
        auto s = db->SetDBOptions(a.db_options);
        if (!s.ok())
            Log("SetDBOptions failed: %s", s.ToString().c_str());
    }
    if (!a.cf_options.empty()) {
        auto s = db->SetOptions(a.cf_options);
        if (!s.ok())
            Log("SetOptions failed: %s", s.ToString().c_str());
    }
}

// ---------------------------------------------------------------------------
// CompactionController
// ---------------------------------------------------------------------------

CompactionController::CompactionController(rocksdb::DB* db)
    : db_(db), running_(false) {}

CompactionController::~CompactionController() {
    Stop();
}

void CompactionController::Start() {
    // exchange returns the old value; if it was already true, do nothing.
    if (running_.exchange(true))
        return;
    thread_ = std::thread(&CompactionController::RunLoop, this);
    Log("started (poll interval = %ds)", kLoopIntervalSeconds);
}

void CompactionController::Stop() {
    if (!running_.exchange(false))
        return;
    if (thread_.joinable())
        thread_.join();
    Log("stopped");
}

void CompactionController::RunLoop() {
    Tier prev_tier = Tier::NOMINAL;

    while (running_.load()) {
        Metrics m = CollectMetrics(db_);
        Tier    t = ClassifyTier(m);

        // Log only on tier transitions to avoid noise.
        if (t != prev_tier) {
            Log("tier %s -> %s  "
                "(l0=%llu  pending=%llu B  delayed_rate=%llu  stopped=%d)",
                TierName(prev_tier), TierName(t),
                (unsigned long long)m.l0_file_count,
                (unsigned long long)m.pending_compaction_bytes,
                (unsigned long long)m.actual_delayed_write_rate,
                (int)m.is_write_stopped);
            prev_tier = t;
        }

        ApplyAction(db_, TierToAction(t));

        std::this_thread::sleep_for(std::chrono::seconds(kLoopIntervalSeconds));
    }
}
