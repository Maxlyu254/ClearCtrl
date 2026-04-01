// tests/bench.cc
//
// Runs a write-heavy workload twice — once without the CompactionController
// (baseline) and once with it — then compares throughput and stall metrics.
//
// Configuration mirrors fillrandom-compaction.sh:
//   2M ops, 1 KB values, 2 threads, 32 MB write buffer, 2 max write buffers,
//   L0 compaction trigger = 4, max_background_jobs = 2, no compression.
//
// Outputs:
//   logs/bench-baseline-<ts>.log    — per-interval metrics + RocksDB stats
//   logs/bench-controlled-<ts>.log  — same, with controller running
//   Both files receive the final side-by-side comparison table.

#include "compaction_controller.h"

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ── Workload config ────────────────────────────────────────────────────────────
// Keep these in sync with fillrandom-compaction.sh.
static constexpr int    kNumThreads         = 2;
static constexpr long   kTotalOps           = 2'000'000;
static constexpr size_t kValueSize          = 1024;
static constexpr size_t kWriteBufferSize    = 32ULL << 20; // 32 MB
static constexpr int    kMaxWriteBuffers    = 2;
static constexpr int    kL0CompactTrigger   = 4;
static constexpr int    kInitialBgJobs      = 2;
static constexpr int    kStatIntervalSec    = 5;

static const char* kBaselineDbPath   = "/tmp/clearctrl-bench-baseline";
static const char* kControlledDbPath = "/tmp/clearctrl-bench-controlled";

// ── Logging ────────────────────────────────────────────────────────────────────
// Writes to both stderr and the run's log file simultaneously.
static void Log(FILE* log, const char* fmt, ...) {
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    for (FILE* f : {stderr, log}) {
        if (!f) continue;
        fprintf(f, "[%s] %s\n", ts, msg);
        fflush(f);
    }
}

// ── Worker thread ──────────────────────────────────────────────────────────────
static std::atomic<long> g_ops_done{0};

static void WorkerThread(rocksdb::DB* db, long num_ops, int tid) {
    // Each thread gets an independent RNG seeded by its id.
    std::mt19937_64 rng(static_cast<uint64_t>(tid) * 6364136223846793005ULL
                        + 1442695040888963407ULL);

    std::string value(kValueSize, '\0');
    rocksdb::WriteOptions wo;
    char key_buf[17];

    for (long i = 0; i < num_ops; ++i) {
        uint64_t k = rng();
        snprintf(key_buf, sizeof(key_buf), "%016llx",
                 static_cast<unsigned long long>(k));
        // Embed the key into the value so blocks aren't trivially compressible.
        memcpy(&value[0], &k, sizeof(k));

        db->Put(wo, rocksdb::Slice(key_buf, 16), value);
        g_ops_done.fetch_add(1, std::memory_order_relaxed);
    }
}

// ── Stats-sampling thread ──────────────────────────────────────────────────────
// Wakes every kStatIntervalSec seconds and logs throughput + DB state.
static void StatsThread(
        rocksdb::DB* db,
        std::chrono::steady_clock::time_point run_start,
        std::atomic<bool>& stop,
        FILE* log) {

    using Clock = std::chrono::steady_clock;
    auto   last_time = run_start;
    long   last_ops  = 0;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kStatIntervalSec));
        if (stop.load(std::memory_order_relaxed)) break;

        auto now     = Clock::now();
        long cur_ops = g_ops_done.load(std::memory_order_relaxed);

        double interval_s  = std::chrono::duration<double>(now - last_time).count();
        double elapsed_s   = std::chrono::duration<double>(now - run_start).count();
        double interval_tp = (cur_ops - last_ops) / interval_s;
        double overall_tp  = cur_ops / elapsed_s;

        uint64_t l0 = 0, pending = 0, delayed = 0, stopped = 0;
        db->GetIntProperty(
            rocksdb::DB::Properties::kNumFilesAtLevelPrefix + "0", &l0);
        db->GetIntProperty(
            rocksdb::DB::Properties::kEstimatePendingCompactionBytes, &pending);
        db->GetIntProperty(
            rocksdb::DB::Properties::kActualDelayedWriteRate, &delayed);
        db->GetIntProperty(
            rocksdb::DB::Properties::kIsWriteStopped, &stopped);

        const char* status = stopped   ? "STOPPED"
                           : delayed   ? "STALLING"
                                       : "ok";

        Log(log,
            "ops %ld/%ld  interval %.0f ops/s  overall %.0f ops/s"
            "  l0=%llu  pending=%llu MB  %s",
            cur_ops, kTotalOps,
            interval_tp, overall_tp,
            static_cast<unsigned long long>(l0),
            static_cast<unsigned long long>(pending >> 20),
            status);

        last_ops  = cur_ops;
        last_time = now;
    }
}

// ── RocksDB options ────────────────────────────────────────────────────────────
static rocksdb::Options BuildOptions() {
    rocksdb::Options opts;
    opts.create_if_missing              = true;
    opts.compression                    = rocksdb::kNoCompression;
    opts.write_buffer_size              = kWriteBufferSize;
    opts.max_write_buffer_number        = kMaxWriteBuffers;
    opts.level0_file_num_compaction_trigger = kL0CompactTrigger;
    opts.max_background_jobs            = kInitialBgJobs;
    opts.statistics                     = rocksdb::CreateDBStatistics();
    return opts;
}

// ── Per-run result ─────────────────────────────────────────────────────────────
struct RunResult {
    double   duration_sec  = 0;
    double   ops_per_sec   = 0;
    uint64_t stall_micros  = 0;  // rocksdb.stall.micros
    uint64_t bytes_written = 0;  // rocksdb.bytes.written
    double   write_p50_us  = 0;  // rocksdb.db.write.micros P50
    double   write_p99_us  = 0;  // rocksdb.db.write.micros P99
};

// ── Core runner ────────────────────────────────────────────────────────────────
static RunResult RunBench(
        const char* db_path,
        bool        use_controller,
        const char* label,
        FILE*       log) {

    Log(log, "=== %s ===", label);
    Log(log, "db=%s  controller=%s", db_path, use_controller ? "ON" : "OFF");
    Log(log, "ops=%ld  threads=%d  value_size=%zu B  write_buffer=%zu MB"
             "  max_write_buffers=%d  l0_trigger=%d  bg_jobs=%d",
        kTotalOps, kNumThreads, kValueSize, kWriteBufferSize >> 20,
        kMaxWriteBuffers, kL0CompactTrigger, kInitialBgJobs);

    // Always start from a clean slate.
    rocksdb::DestroyDB(db_path, rocksdb::Options{});

    rocksdb::Options opts = BuildOptions();
    std::unique_ptr<rocksdb::DB> db_owner;
    auto s = rocksdb::DB::Open(opts, db_path, &db_owner);
    if (!s.ok()) {
        Log(log, "ERROR: DB::Open failed: %s", s.ToString().c_str());
        return {};
    }
    rocksdb::DB* db = db_owner.get();

    // Optionally attach the controller before writes begin.
    std::unique_ptr<CompactionController> ctl;
    if (use_controller) {
        ctl = std::make_unique<CompactionController>(db);
        ctl->Start();
    }

    g_ops_done.store(0, std::memory_order_relaxed);

    auto run_start = std::chrono::steady_clock::now();

    // Stats-sampling thread.
    std::atomic<bool> stats_stop{false};
    std::thread stats_thr(StatsThread, db, run_start, std::ref(stats_stop), log);

    // Worker threads — distribute ops evenly; last thread takes remainder.
    std::vector<std::thread> workers;
    workers.reserve(kNumThreads);
    long base_ops = kTotalOps / kNumThreads;
    for (int i = 0; i < kNumThreads; ++i) {
        long ops = base_ops + (i == kNumThreads - 1 ? kTotalOps % kNumThreads : 0);
        workers.emplace_back(WorkerThread, db, ops, i);
    }
    for (auto& w : workers) w.join();

    auto run_end = std::chrono::steady_clock::now();

    stats_stop.store(true, std::memory_order_relaxed);
    stats_thr.join();

    if (ctl) ctl->Stop();

    // Collect final statistics.
    auto* stats = opts.statistics.get();

    RunResult r;
    r.duration_sec  = std::chrono::duration<double>(run_end - run_start).count();
    r.ops_per_sec   = kTotalOps / r.duration_sec;
    r.stall_micros  = stats->getTickerCount(rocksdb::STALL_MICROS);
    r.bytes_written = stats->getTickerCount(rocksdb::BYTES_WRITTEN);

    rocksdb::HistogramData hist{};
    stats->histogramData(rocksdb::DB_WRITE, &hist);
    r.write_p50_us = hist.median;
    r.write_p99_us = hist.percentile99;

    // Write the full RocksDB statistics block to the log file.
    Log(log, "--- full RocksDB statistics ---");
    fputs(stats->ToString().c_str(), log);
    fputc('\n', log);
    fflush(log);

    db_owner.reset();  // close DB before destroying
    rocksdb::DestroyDB(db_path, rocksdb::Options{});

    return r;
}

// ── Summary helpers ────────────────────────────────────────────────────────────
static void PrintResult(FILE* log, const char* label, const RunResult& r) {
    Log(log, "──────────────────────────────────");
    Log(log, "Result  : %s", label);
    Log(log, "  duration      : %.2f s",     r.duration_sec);
    Log(log, "  throughput    : %.0f ops/s", r.ops_per_sec);
    Log(log, "  bytes written : %llu MB",
        static_cast<unsigned long long>(r.bytes_written >> 20));
    Log(log, "  stall time    : %llu ms",
        static_cast<unsigned long long>(r.stall_micros / 1000));
    Log(log, "  write p50     : %.1f us",    r.write_p50_us);
    Log(log, "  write p99     : %.1f us",    r.write_p99_us);
    Log(log, "──────────────────────────────────");
}

static void PrintComparison(FILE* log,
                            const RunResult& baseline,
                            const RunResult& controlled) {
    auto pct = [](double a, double b) -> double {
        return a != 0 ? 100.0 * (b - a) / a : 0.0;
    };

    fprintf(log, "\n=== Comparison: baseline vs controlled ===\n");
    fprintf(log, "%-22s  %12s  %12s  %8s\n",
            "Metric", "Baseline", "Controlled", "Delta");
    fprintf(log, "%-22s  %12.0f  %12.0f  %+7.1f%%\n",
            "throughput (ops/s)",
            baseline.ops_per_sec, controlled.ops_per_sec,
            pct(baseline.ops_per_sec, controlled.ops_per_sec));
    fprintf(log, "%-22s  %12.2f  %12.2f  %+7.1f%%\n",
            "duration (s)",
            baseline.duration_sec, controlled.duration_sec,
            pct(baseline.duration_sec, controlled.duration_sec));
    fprintf(log, "%-22s  %12llu  %12llu  %+7.1f%%\n",
            "stall time (ms)",
            static_cast<unsigned long long>(baseline.stall_micros  / 1000),
            static_cast<unsigned long long>(controlled.stall_micros / 1000),
            pct(static_cast<double>(baseline.stall_micros),
                static_cast<double>(controlled.stall_micros)));
    fprintf(log, "%-22s  %12.1f  %12.1f\n",
            "write p50 (us)",
            baseline.write_p50_us, controlled.write_p50_us);
    fprintf(log, "%-22s  %12.1f  %12.1f\n",
            "write p99 (us)",
            baseline.write_p99_us, controlled.write_p99_us);
    fflush(log);
}

// ── main ───────────────────────────────────────────────────────────────────────
int main() {
    // Timestamped log file names.
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm_buf);

    char baseline_path[256], controlled_path[256];
    snprintf(baseline_path,   sizeof(baseline_path),
             "logs/bench-baseline-%s.log",   ts);
    snprintf(controlled_path, sizeof(controlled_path),
             "logs/bench-controlled-%s.log", ts);

    FILE* baseline_log   = fopen(baseline_path,   "w");
    FILE* controlled_log = fopen(controlled_path, "w");
    if (!baseline_log || !controlled_log) {
        fprintf(stderr, "ERROR: cannot open log files — does logs/ exist?\n");
        return 1;
    }

    fprintf(stderr, "Logging baseline   -> %s\n", baseline_path);
    fprintf(stderr, "Logging controlled -> %s\n", controlled_path);

    RunResult baseline   = RunBench(kBaselineDbPath,   false,
                                    "baseline (no controller)",     baseline_log);
    RunResult controlled = RunBench(kControlledDbPath, true,
                                    "controlled (with controller)", controlled_log);

    PrintResult(baseline_log,   "baseline",   baseline);
    PrintResult(controlled_log, "controlled", controlled);

    // Write the comparison table to both log files and stderr.
    for (FILE* f : {stderr, baseline_log, controlled_log})
        PrintComparison(f, baseline, controlled);

    fclose(baseline_log);
    fclose(controlled_log);
    return 0;
}
