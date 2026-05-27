#ifndef OCC_ENGINE_H
#define OCC_ENGINE_H

#include "dsm_object.h"
#include <stdint.h>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <atomic>

enum AbortReason {
    ABORT_LOCK_FAIL = 0,
    ABORT_VALIDATION_FAIL = 1,
    ABORT_BUSINESS_ABORT = 2,
    ABORT_MAX_RETRY = 3,
    ABORT_SYSTEM_ERROR = 4
};

enum PathType {
    COLD_PATH = 0,
    HOT_PATH = 1
};

struct Transaction {
    uint64_t tx_id;
    std::vector<uint64_t> read_set_ids;
    std::vector<uint64_t> read_set_versions;
    std::map<uint64_t, uint64_t> write_set;
    uint32_t retry_count;
    AbortReason abort_reason;
    uint64_t start_time_us;
    uint64_t commit_time_us;
    PathType path_type;
    bool committed;
};

class OCCEngine {
public:
    OCCEngine(DSMObjectStore* store);
    ~OCCEngine();

    // Transaction lifecycle
    void begin_transaction(Transaction& tx);
    int read_object(Transaction& tx, uint64_t object_id, uint64_t& value, uint64_t& version);
    void write_object(Transaction& tx, uint64_t object_id, uint64_t value);
    int commit_transaction(Transaction& tx);

    // Helper: read and validate in one call
    int read_with_lock(uint64_t object_id, uint64_t& value, uint64_t& version);

    // Get current time in microseconds
    static uint64_t get_time_us() {
        auto now = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
        return us.count();
    }

private:
    DSMObjectStore* store_;
    std::atomic<uint64_t> next_tx_id_;

    // OCC commit phase: try to acquire locks and validate
    int try_acquire_locks(Transaction& tx);
    int validate_read_set(Transaction& tx);
    int apply_write_set(Transaction& tx);
    void release_locks(Transaction& tx);
};

#endif // OCC_ENGINE_H
