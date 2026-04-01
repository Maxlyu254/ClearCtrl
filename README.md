

We use RocksDB v10.10.1 (official signed release).

environment:
google cloud n2-standard-2 (2 vCPU, 8 GB)

apt install -y libgflags-dev

make -j2 static_lib DISABLE_WARNING_AS_ERROR=1

make -j2 db_bench DEBUG_LEVEL=0 LIB_MODE=static DISABLE_WARNING_AS_ERROR=1

The user passes a `rocksdb::DB*` pointer to your controller module, and you launch a background thread to perform periodic monitoring and parameter tuning.
More precisely, however, you are monitoring RocksDB's internal state.

We provide a controller library. After creating and opening a RocksDB DB instance, the user passes the `DB*` pointer to the controller. The controller then launches a background thread to periodically read RocksDB's internal state and dynamically adjust compaction-related parameters via the runtime options API.

First, verify two things:

1. Which metrics can be read via the API?

2. Which parameters can be modified at runtime via the API?

These two factors determine whether your controller can operate as a closed-loop system.

In other words, your next step should be to:

Compile a table of "observable metrics / controllable knobs."

user thread:
```cpp
rocksdb::DB* db;
rocksdb::DB::Open(..., &db);
db->Put(...);
db->Get(...);

CompactionController ctl(db);
ctl.Start();
```

our thread:
```cpp
while (true) {
    auto stall = get_stall();
    auto backlog = get_l0_files();

    if (stall > threshold) {
        db->SetOptions({{"max_background_jobs", "4"}});
    } else {
        db->SetOptions({{"max_background_jobs", "2"}});
    }

    sleep(5);
}
```

---

## Building

Dependencies (Ubuntu):

```bash
apt install -y libgflags-dev libzstd-dev libsnappy-dev liblz4-dev libbz2-dev
```

Build the controller library (`libclearctrl.a`):

```bash
make
```

`ROCKSDB_DIR` defaults to `~/cs525/project/rocksdb`. Override it if your RocksDB tree is elsewhere:

```bash
make ROCKSDB_DIR=/path/to/rocksdb
```

RocksDB v10.10.1 requires C++20. The Makefile sets `-std=c++20` automatically.

## Testing

Build and run the benchmark:

```bash
make test
```

This compiles `tests/bench.cc` into a `bench` binary, then runs it. The benchmark runs the same `fillrandom` workload twice — once without the controller (baseline) and once with it — and writes results to `logs/`:

```
logs/bench-baseline-<timestamp>.log
logs/bench-controlled-<timestamp>.log
```

Both files receive a final side-by-side comparison table. Progress is also printed to stderr during the run.

The workload configuration matches `scripts/fillrandom-compaction.sh`: 2M random writes, 1 KB values, 2 threads, 32 MB write buffer, `max_background_jobs=2`, no compression.
