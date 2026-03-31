#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/options.h"

#include "compaction_controller.h"

using namespace ROCKSDB_NAMESPACE;


void signal_handler(int) {
    g_stop.store(true);
}


int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const std::string kDBPath = "./testdb";

    Options options;
    options.create_if_missing = true;

    // 先给一个比较容易观察 compaction/backlog 的配置
    options.IncreaseParallelism(2);
    options.OptimizeLevelStyleCompaction();

    // 为了更容易让 L0 累积，给一个相对激进一点的配置
    options.write_buffer_size = 4 * 1024 * 1024;   // 4MB
    // options.max_write_buffer_number = 3;
    options.level0_file_num_compaction_trigger = 12; // 必须大于 l0_threshold=6
    options.level0_slowdown_writes_trigger = 20;
    options.level0_stop_writes_trigger = 36;

    DB* db = nullptr;
    Status s = DB::Open(options, kDBPath, &db);
    if (!s.ok()) {
        std::cerr << "Open DB failed: " << s.ToString() << std::endl;
        return 1;
    }

    std::unique_ptr<DB> db_guard(db);

    // 做一点基本读写，确认 DB 正常
    s = db->Put(WriteOptions(), "hello", "world");
    if (!s.ok()) {
        std::cerr << "Put failed: " << s.ToString() << std::endl;
        return 1;
    }

    std::string value;
    s = db->Get(ReadOptions(), "hello", &value);
    if (!s.ok()) {
        std::cerr << "Get failed: " << s.ToString() << std::endl;
        return 1;
    }
    std::cout << "Get(hello) = " << value << std::endl;

    // 启动 controller
    CompactionController ctl(
        db,
        6,  // l0_threshold
        2,  // low_bg_jobs
        4,  // high_bg_jobs
        5   // interval_sec
    );
    ctl.Start();

    // 前台 workload：持续写入，推动 flush/L0/compaction 发生
    uint64_t i = 0;
    while (!g_stop.load()) {
        WriteBatch batch;
        for (int j = 0; j < 1000; ++j) {
            std::string key = "key_" + std::to_string(i++);
            std::string val(1024, 'x');  // 1KB value
            batch.Put(key, val);
        }

        s = db->Write(WriteOptions(), &batch);
        if (!s.ok()) {
            std::cerr << "Write failed: " << s.ToString() << std::endl;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ctl.Stop();

    Status close_status = db->Close();
    if (!close_status.ok()) {
        std::cerr << "Close failed: " << close_status.ToString() << std::endl;
        return 1;
    }

    return 0;
}