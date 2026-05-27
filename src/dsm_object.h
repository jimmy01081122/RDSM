#ifndef DSM_OBJECT_H
#define DSM_OBJECT_H

#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>

enum ObjectType {
    TYPE_PRODUCT_STOCK = 1,
    TYPE_USER_BALANCE = 2,
    TYPE_SOLD_COUNT = 3
};

struct ObjectHeader {
    uint64_t object_id;
    uint64_t version;
    uint64_t lock_owner;        // tx_id that holds the lock
    uint32_t lock_bit;          // 1 = locked, 0 = free
    uint32_t object_type;
    uint64_t last_writer_tx_id;
    uint64_t value;             // Embedded value
} __attribute__((packed));

static_assert(sizeof(ObjectHeader) == 48, "ObjectHeader must be 48 bytes");

class DSMObjectStore {
public:
    DSMObjectStore();
    ~DSMObjectStore();

    // Create object (server-side)
    int create_object(uint64_t object_id, ObjectType type, uint64_t init_value);

    // Get object header (includes current version & lock status)
    ObjectHeader* get_object_header(uint64_t object_id);

    // RDMA-visible: get base address of all objects (for RDMA operations)
    void* get_objects_addr() const { return objects_; }
    uint32_t get_objects_size() const { return max_objects_ * sizeof(ObjectHeader); }

    // Transaction metadata
    struct TransactionMetadata {
        uint64_t tx_id;
        std::vector<uint64_t> read_set_versions;  // versions read during read phase
        std::map<uint64_t, uint64_t> write_set;   // object_id -> new_value
        uint64_t start_time_us;
        uint64_t commit_time_us;
    };

    // Per-object statistics
    struct ObjectStats {
        std::atomic<uint64_t> access_count{0};
        std::atomic<uint64_t> write_count{0};
        std::atomic<uint64_t> lock_fail_count{0};
        std::atomic<uint64_t> validation_fail_count{0};
        std::atomic<uint64_t> abort_count{0};
        std::atomic<uint64_t> last_hot_timestamp{0};
        bool is_hot;
    };

    ObjectStats* get_object_stats(uint64_t object_id);

    // Global statistics
    struct GlobalStats {
        std::atomic<uint64_t> attempted_tx{0};
        std::atomic<uint64_t> committed_tx{0};
        std::atomic<uint64_t> aborted_tx{0};
        std::atomic<uint64_t> business_abort_tx{0};
        std::atomic<uint64_t> retry_count{0};
        std::atomic<uint64_t> lock_fail_count{0};
        std::atomic<uint64_t> validation_fail_count{0};
        std::atomic<uint64_t> invariant_violation_count{0};
        std::atomic<uint64_t> duplicate_commit_count{0};
        std::atomic<uint64_t> total_commit_latency_us{0};
        std::atomic<uint64_t> hot_path_tx{0};
        std::atomic<uint64_t> cold_path_tx{0};
        std::vector<uint64_t> latency_histogram;  // [0-10us, 10-50us, 50-100us, 100-500us, 500+us]
    };

    GlobalStats* get_global_stats() { return &global_stats_; }

    // Utility
    uint64_t get_object_count() const { return object_count_; }

    // Verification (after run)
    int verify_invariants(uint64_t initial_stock, uint64_t& final_stock, uint64_t& sold_count);

private:
    static constexpr uint32_t MAX_OBJECTS = 1000;

    ObjectHeader* objects_;           // RDMA-visible memory
    uint32_t max_objects_;
    uint64_t object_count_;
    std::map<uint64_t, ObjectStats> object_stats_;
    std::mutex stats_mutex_;
    GlobalStats global_stats_;
};

#endif // DSM_OBJECT_H
