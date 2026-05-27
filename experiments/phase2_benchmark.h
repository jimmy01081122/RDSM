#ifndef PHASE2_BENCHMARK_H
#define PHASE2_BENCHMARK_H

#include "occ_engine.h"
#include "dsm_object.h"
#include <vector>
#include <string>
#include <map>
#include <cstdint>

struct BenchmarkConfig {
    // Workload parameters
    uint32_t num_products;
    uint32_t num_users;
    uint32_t num_threads;
    double write_ratio;
    std::string access_pattern;  // uniform, zipfian_0.8, zipfian_0.99
    uint32_t duration_sec;
    uint32_t max_retries;

    // Algorithm selection
    std::string algorithm;  // baseline_occ, backoff_occ, hot_detection_occ, hybrid_arbitration_occ

    // Backoff parameters
    std::string backoff_policy;  // NO_BACKOFF, FIXED_BACKOFF, EXPONENTIAL_BACKOFF, CONTENTION_AWARE_BACKOFF
    uint32_t backoff_base_us;
    uint32_t backoff_max_us;

    // Hot detection parameters
    bool hot_detection_enabled;
    double hot_threshold;
    uint32_t hot_window_ms;
    uint32_t hot_min_access;

    // Hybrid arbitration parameters
    bool hybrid_enabled;
    std::string arbitration_mode;  // fifo
    uint32_t server_queue_size;
    uint32_t server_worker_threads;

    // Derived
    std::vector<uint32_t> hot_product_ids;
};

class InventoryWorkload {
public:
    InventoryWorkload(const BenchmarkConfig& config, DSMObjectStore* store, OCCEngine* engine);
    ~InventoryWorkload();

    // Generate next transaction (inventory order attempt)
    void generate_order_transaction(Transaction& tx, int thread_id);

    // Inventory data layout:
    // Objects 0-(num_products-1): product_stock[i]
    // Objects num_products-(num_products+num_users-1): user_balance[i]
    // Object (num_products+num_users): sold_count

    struct OrderInfo {
        uint32_t product_id;
        uint32_t user_id;
        uint64_t price;
        bool is_read_only;
    };

    OrderInfo generate_order(int thread_id);

private:
    const BenchmarkConfig& config_;
    DSMObjectStore* store_;
    OCCEngine* engine_;
};

struct RunResult {
    // Counters
    uint64_t attempted_tx;
    uint64_t committed_tx;
    uint64_t aborted_tx;
    uint64_t business_abort_tx;

    // Ratios
    double abort_rate;
    double retry_per_commit;

    // Latency
    double latency_p50_us;
    double latency_p95_us;
    double latency_p99_us;

    // Hot path
    uint64_t hot_object_count;
    uint64_t hot_path_tx;
    uint64_t cold_path_tx;
    double hot_path_ratio;

    // Server queue (if hybrid)
    double server_queue_wait_p50_us;
    double server_queue_wait_p95_us;
    double server_queue_wait_p99_us;

    // Correctness
    uint64_t invariant_violation_count;
    uint64_t duplicate_commit_count;
    uint64_t final_stock;
    uint64_t sold_count;
};

class BenchmarkRunner {
public:
    BenchmarkRunner(const BenchmarkConfig& config);
    ~BenchmarkRunner();

    int initialize_server();
    int initialize_client();
    int run_benchmark();
    RunResult get_result();

private:
    BenchmarkConfig config_;
    std::unique_ptr<DSMObjectStore> store_;
    std::unique_ptr<OCCEngine> engine_;
    std::unique_ptr<InventoryWorkload> workload_;
};

#endif // PHASE2_BENCHMARK_H
