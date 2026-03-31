#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/status.h"


static std::atomic<bool> g_stop{false};

class CompactionController {
public:
    CompactionController(rocksdb::DB* db,
                            int l0_threshold,
                            int low_bg_jobs,
                            int high_bg_jobs,
                            int interval_sec)
        : db_(db),
            l0_threshold_(l0_threshold),
            low_bg_jobs_(low_bg_jobs),
            high_bg_jobs_(high_bg_jobs),
            interval_sec_(interval_sec),
            current_bg_jobs_(-1) {}

    void Start() {
        worker_ = std::thread(&CompactionController::Run, this);
    }

    void Stop() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    uint64_t GetL0Files() {
        std::string prop;
        bool ok = db_->GetProperty("rocksdb.num-files-at-level0", &prop);
        if (!ok) {
            std::cerr << "[controller] failed to read property "
                        << "rocksdb.num-files-at-level0" << std::endl;
            return 0;
        }
        return std::stoull(prop);
    }

    void ApplyMaxBackgroundJobs(int jobs) {
        if (jobs == current_bg_jobs_) {
            return;  // 避免重复设置
        }

        rocksdb::Status s = db_->SetDBOptions({
            {"max_background_jobs", std::to_string(jobs)}
        });

        if (!s.ok()) {
            std::cerr << "[controller] SetDBOptions failed: "
                        << s.ToString() << std::endl;
            return;
        }

        current_bg_jobs_ = jobs;
        std::cout << "[controller] applied max_background_jobs="
                    << jobs << std::endl;
    }

    void Run() {
        while (!g_stop.load()) {
            uint64_t l0 = GetL0Files();

            int target_jobs = (l0 > static_cast<uint64_t>(l0_threshold_))
                                ? high_bg_jobs_
                                : low_bg_jobs_;

            std::cout << "[controller] L0 files=" << l0
                        << ", threshold=" << l0_threshold_
                        << ", target max_background_jobs=" << target_jobs
                        << std::endl;

            ApplyMaxBackgroundJobs(target_jobs);

            for (int i = 0; i < interval_sec_ && !g_stop.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        std::cout << "[controller] stopping" << std::endl;
    }

private:
    rocksdb::DB* db_;
    int l0_threshold_;
    int low_bg_jobs_;
    int high_bg_jobs_;
    int interval_sec_;
    int current_bg_jobs_;
    std::thread worker_;
};