#ifndef DSM_OBJECT_H
#define DSM_OBJECT_H

#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>

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
        std::atomic<bool> is_hot{false};
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
        std::atomic<uint64_t> server_arbitrated_tx{0};
        std::atomic<uint64_t> hot_path_candidate_tx{0};
        std::atomic<uint64_t> server_queue_wait_total_us{0};
        std::atomic<uint64_t> server_queue_wait_count{0};
        std::atomic<uint64_t> server_queue_wait_max_us{0};
        std::atomic<uint64_t> server_service_time_total_us{0};
        std::atomic<uint64_t> server_service_time_count{0};
        std::atomic<uint64_t> server_service_time_max_us{0};
        std::atomic<uint64_t> server_queue_length_max{0};
        std::atomic<uint64_t> hot_cold_interference_count{0};
        std::vector<uint64_t> latency_histogram;  // [0-10us, 10-50us, 50-100us, 100-500us, 500+us]
    };

    GlobalStats* get_global_stats() { return &global_stats_; }

    // Utility
    uint64_t get_object_count() const { return object_count_; }

    // Verification (after run)
    int verify_invariants(uint64_t initial_stock, uint64_t& final_stock, uint64_t& sold_count);

    // Coarse-grained stand-in for remote atomic/object mutation serialization in this prototype.
    std::mutex& mutation_mutex() { return mutation_mutex_; }
    std::mutex& object_mutex(uint64_t object_id);
    std::mutex& global_arbitration_mutex() { return global_arbitration_mutex_; }
    std::mutex& object_arbitration_mutex(uint64_t object_id);
    std::mutex& shard_arbitration_mutex(uint64_t shard_id);
    std::atomic<uint64_t>& global_queue_depth() { return global_queue_depth_; }
    std::atomic<uint64_t>& object_queue_depth(uint64_t object_id);
    std::atomic<uint64_t>& shard_queue_depth(uint64_t shard_id);
    void record_queue_wait_sample(uint64_t value_us);
    void record_queue_length_sample(uint64_t value);
    void record_service_time_sample(uint64_t value_us);
    std::vector<uint64_t> queue_wait_samples();
    std::vector<uint64_t> queue_length_samples();
    std::vector<uint64_t> service_time_samples();

private:
    static constexpr uint32_t MAX_OBJECTS = 1000;
    static constexpr uint32_t MAX_ARBITRATION_SHARDS = 32;
    static constexpr uint32_t MAX_SAMPLES = 200000;

    ObjectHeader* objects_;           // RDMA-visible memory
    uint32_t max_objects_;
    uint64_t object_count_;
    std::map<uint64_t, ObjectStats> object_stats_;
    std::mutex stats_mutex_;
    std::mutex mutation_mutex_;
    std::mutex global_arbitration_mutex_;
    std::atomic<uint64_t> global_queue_depth_{0};
    std::vector<std::unique_ptr<std::mutex>> object_mutexes_;
    std::vector<std::unique_ptr<std::mutex>> object_arbitration_mutexes_;
    std::vector<std::unique_ptr<std::mutex>> shard_arbitration_mutexes_;
    std::vector<std::atomic<uint64_t>> object_queue_depths_;
    std::vector<std::atomic<uint64_t>> shard_queue_depths_;
    std::mutex sample_mutex_;
    std::vector<uint64_t> queue_wait_samples_;
    std::vector<uint64_t> queue_length_samples_;
    std::vector<uint64_t> service_time_samples_;
    GlobalStats global_stats_;
};

#endif // DSM_OBJECT_H
