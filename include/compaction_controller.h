#pragma once

#include <atomic>
#include <thread>

namespace rocksdb { class DB; }

// CompactionController monitors a RocksDB instance in a background thread and
// dynamically adjusts compaction-related parameters to reduce write stalls.
//
// Usage:
//   rocksdb::DB* db;
//   rocksdb::DB::Open(..., &db);
//
//   CompactionController ctl(db);
//   ctl.Start();
//   // ... application runs ...
//   ctl.Stop();
//
// The controller must not outlive the DB pointer it was given.
class CompactionController {
public:
    explicit CompactionController(rocksdb::DB* db);
    ~CompactionController();

    // Launch the background monitoring thread. No-op if already running.
    void Start();

    // Signal the background thread to stop and block until it exits.
    void Stop();

private:
    void RunLoop();

    rocksdb::DB*      db_;
    std::thread       thread_;
    std::atomic<bool> running_;
};
