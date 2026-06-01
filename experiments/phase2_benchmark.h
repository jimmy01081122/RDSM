#ifndef PHASE2_BENCHMARK_H
#define PHASE2_BENCHMARK_H

#include "occ_engine.h"
#include "dsm_object.h"
#include "latency_sampler.h"
#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>

struct BenchmarkConfig {
    // Workload parameters
    uint32_t num_products;
    uint32_t num_users;
    uint32_t num_threads;
    double write_ratio;
    std::string access_pattern;  // uniform, zipfian_0.8, zipfian_0.99
    std::string application_case;
    double hot_access_probability;
    uint32_t duration_sec;
    uint32_t max_retries;
    std::string sold_counter_mode;  // global, per_product
    std::string workload_name;
    bool appendix_only;
    std::string appendix_reason;

    // Latency sampling
    LatencySamplingMode latency_sampling_mode;
    uint32_t latency_sample_size;
    std::string latency_output;
    bool allow_dangerous_full_sampling;

    // Algorithm selection
    std::string algorithm;  // baseline_occ, backoff_occ, hot_detection_occ, hybrid_arbitration_occ, hybrid_adaptive_arbitration_occ

    // Backoff parameters
    std::string backoff_policy;  // NO_BACKOFF, FIXED_BACKOFF, EXPONENTIAL_BACKOFF, CONTENTION_AWARE_BACKOFF
    uint32_t backoff_base_us;
    uint32_t backoff_max_us;

    // Hot detection parameters
    bool hot_detection_enabled;
    double hot_threshold;
    uint32_t hot_window_ms;
    uint32_t hot_min_access;
    uint32_t hot_refresh_interval;

    // Hybrid arbitration parameters
    bool hybrid_enabled;
    std::string arbitration_mode;  // global, per_object, per_shard
    uint32_t hot_shards;
    uint32_t server_queue_size;
    uint32_t server_worker_threads;

    // Adaptive routing parameters
    bool adaptive_routing_enabled;
    double routing_margin_us;
    uint32_t cost_window_ms;
    uint32_t min_samples_before_adapt;
    std::string adaptive_object_scope;  // global, shard, object

    // Derived
    std::vector<uint32_t> hot_product_ids;
};

struct AdaptiveRoutingState {
    double ewma_occ_latency_us{1.0};
    double ewma_retry_per_tx{0.0};
    double ewma_queue_wait_us{0.0};
    double ewma_service_time_us{1.0};
    uint64_t samples{0};
    bool has_last_route{false};
    bool last_route_arbitration{false};
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
    uint64_t logical_tx;
    uint64_t occ_attempts;
    uint64_t occ_failed_attempts;
    uint64_t final_abort_tx;
    // Compatibility aliases: attempted_tx == logical_tx, aborted_tx == final_abort_tx.
    uint64_t attempted_tx;
    uint64_t committed_tx;
    uint64_t aborted_tx;
    uint64_t business_abort_tx;

    // Ratios
    double abort_rate;
    double retry_per_commit;

    // Latency
    LatencySummary latency_summary;

    // Hot path
    uint64_t hot_object_count;
    uint64_t hot_path_tx;
    uint64_t cold_path_tx;
    double hot_path_ratio;

    // Server queue (if hybrid)
    double server_queue_wait_p50_us;
    double server_queue_wait_p95_us;
    double server_queue_wait_p99_us;
    double server_queue_wait_max_us;
    double queue_length_p50;
    double queue_length_p95;
    double queue_length_p99;
    double service_time_p50_us;
    double service_time_p95_us;
    double service_time_p99_us;
    double service_time_max_us;
    uint64_t hot_cold_interference_count;

    // Adaptive routing
    uint64_t adaptive_route_to_occ_count;
    uint64_t adaptive_route_to_arbitration_count;
    double adaptive_route_to_occ_ratio;
    double adaptive_route_to_arbitration_ratio;
    uint64_t adaptive_insufficient_samples_count;
    uint64_t adaptive_bad_route_proxy_count;
    double estimated_occ_cost_us_p50;
    double estimated_occ_cost_us_p95;
    double estimated_occ_cost_us_p99;
    double estimated_arbitration_cost_us_p50;
    double estimated_arbitration_cost_us_p95;
    double estimated_arbitration_cost_us_p99;
    double routing_decision_latency_us_p50;
    double routing_decision_latency_us_p95;
    double routing_decision_latency_us_p99;
    uint64_t oscillation_count;

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
    DSMObjectStore* get_store() { return store_.get(); }

private:
    BenchmarkConfig config_;
    std::unique_ptr<DSMObjectStore> store_;
    std::unique_ptr<OCCEngine> engine_;
    std::unique_ptr<InventoryWorkload> workload_;
    std::unique_ptr<LatencySampler> latency_sampler_;
    mutable std::mutex adaptive_mutex_;
    std::vector<AdaptiveRoutingState> adaptive_states_;
    std::vector<uint64_t> estimated_occ_cost_samples_;
    std::vector<uint64_t> estimated_arbitration_cost_samples_;
    std::vector<uint64_t> routing_decision_latency_samples_;

    uint32_t adaptive_scope_index(uint64_t product_id) const;
    bool choose_adaptive_route(uint64_t product_id, bool hot_candidate);
    void update_adaptive_state(uint64_t product_id, bool routed_to_arbitration);
};

#endif // PHASE2_BENCHMARK_H
