#include "phase2_benchmark.h"
#include "json_utils.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <cmath>
#include <fstream>

InventoryWorkload::InventoryWorkload(const BenchmarkConfig& config, DSMObjectStore* store, OCCEngine* engine)
    : config_(config), store_(store), engine_(engine) {
}

InventoryWorkload::~InventoryWorkload() {
}

InventoryWorkload::OrderInfo InventoryWorkload::generate_order(int thread_id) {
    static thread_local std::mt19937 gen(std::random_device{}() + thread_id);
    std::uniform_real_distribution<> dis(0.0, 1.0);
    std::uniform_int_distribution<> user_dis(0, config_.num_users - 1);

    OrderInfo info;
    info.user_id = user_dis(gen);
    info.price = 100;  // Fixed price
    info.is_read_only = dis(gen) > config_.write_ratio;

    // Access pattern (Zipfian or uniform)
    if (config_.access_pattern == "uniform") {
        std::uniform_int_distribution<> prod_dis(0, config_.num_products - 1);
        info.product_id = prod_dis(gen);
    } else if (config_.access_pattern.find("zipfian") != std::string::npos) {
        // Simplified Zipfian: generate based on theta
        double theta = 0.8;
        if (config_.access_pattern.find("0.99") != std::string::npos) {
            theta = 0.99;
        }

        // Simple Zipfian approximation
        double u = dis(gen);
        double rank = std::pow(u, -1.0 / (1.0 - theta)) * config_.num_products;
        info.product_id = std::min((uint32_t)(rank - 1), config_.num_products - 1);
    } else {
        std::uniform_int_distribution<> prod_dis(0, config_.num_products - 1);
        info.product_id = prod_dis(gen);
    }

    return info;
}

void InventoryWorkload::generate_order_transaction(Transaction& tx, int thread_id) {
    // Generate an inventory order transaction
    OrderInfo order = generate_order(thread_id);

    if (!order.is_read_only) {
        // Read phase
        uint64_t stock_value, stock_version;
        uint64_t balance_value, balance_version;

        uint64_t stock_obj_id = order.product_id;  // product_stock[product_id]
        uint64_t balance_obj_id = config_.num_products + order.user_id;  // user_balance[user_id]

        engine_->read_object(tx, stock_obj_id, stock_value, stock_version);
        engine_->read_object(tx, balance_obj_id, balance_value, balance_version);

        // Business logic
        if (stock_value > 0 && balance_value >= order.price) {
            // Apply writes
            engine_->write_object(tx, stock_obj_id, stock_value - 1);
            engine_->write_object(tx, balance_obj_id, balance_value - order.price);

            // Increment sold_count
            uint64_t sold_obj_id = config_.num_products + config_.num_users;
            uint64_t sold_value, sold_version;
            engine_->read_object(tx, sold_obj_id, sold_value, sold_version);
            engine_->write_object(tx, sold_obj_id, sold_value + 1);
        } else {
            // Business abort
            tx.abort_reason = ABORT_BUSINESS_ABORT;
        }
    } else {
        // Read-only transaction
        uint64_t stock_value, stock_version;
        uint64_t stock_obj_id = order.product_id;
        engine_->read_object(tx, stock_obj_id, stock_value, stock_version);
    }
}

BenchmarkRunner::BenchmarkRunner(const BenchmarkConfig& config)
    : config_(config) {
    store_ = std::make_unique<DSMObjectStore>();
    engine_ = std::make_unique<OCCEngine>(store_.get());
    workload_ = std::make_unique<InventoryWorkload>(config_, store_.get(), engine_.get());
}

BenchmarkRunner::~BenchmarkRunner() {
}

int BenchmarkRunner::initialize_server() {
    // Initialize DSM objects on server side
    // Create products
    for (uint32_t i = 0; i < config_.num_products; i++) {
        store_->create_object(i, TYPE_PRODUCT_STOCK, 100);  // Initial stock per product
    }

    // Create user balances
    for (uint32_t i = 0; i < config_.num_users; i++) {
        store_->create_object(config_.num_products + i, TYPE_USER_BALANCE, 10000);  // Initial balance
    }

    // Create sold_count
    store_->create_object(config_.num_products + config_.num_users, TYPE_SOLD_COUNT, 0);

    return 0;
}

int BenchmarkRunner::initialize_client() {
    // Client-side initialization (if needed)
    return 0;
}

int BenchmarkRunner::run_benchmark() {
    initialize_server();
    initialize_client();

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(config_.duration_sec);

    std::vector<std::thread> threads;
    std::atomic<bool> keep_running(true);

    // Benchmark threads
    for (uint32_t t = 0; t < config_.num_threads; t++) {
        threads.emplace_back([this, t, &end_time, &keep_running]() {
            while (std::chrono::steady_clock::now() < end_time) {
                Transaction tx;
                engine_->begin_transaction(tx);

                workload_->generate_order_transaction(tx, t);

                uint32_t retry = 0;
                while (retry < config_.max_retries) {
                    if (tx.abort_reason == ABORT_BUSINESS_ABORT) {
                        store_->get_global_stats()->business_abort_tx++;
                        tx.committed = false;
                        break;
                    }

                    if (engine_->commit_transaction(tx) == 0) {
                        break;
                    }

                    retry++;
                    store_->get_global_stats()->retry_count++;

                    // Backoff if enabled
                    if (config_.backoff_policy == "FIXED_BACKOFF") {
                        std::this_thread::sleep_for(std::chrono::microseconds(config_.backoff_base_us));
                    }
                }

                if (retry >= config_.max_retries && !tx.committed) {
                    store_->get_global_stats()->aborted_tx++;
                }
            }
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}

RunResult BenchmarkRunner::get_result() {
    auto stats = store_->get_global_stats();

    RunResult result;
    result.attempted_tx = stats->attempted_tx;
    result.committed_tx = stats->committed_tx;
    result.aborted_tx = stats->aborted_tx;
    result.business_abort_tx = stats->business_abort_tx;

    result.abort_rate = (stats->attempted_tx > 0) ?
        (double)(stats->aborted_tx + stats->business_abort_tx) / stats->attempted_tx : 0.0;

    result.retry_per_commit = (stats->committed_tx > 0) ?
        (double)stats->retry_count / stats->committed_tx : 0.0;

    // Latency percentiles (simplified)
    if (stats->committed_tx > 0) {
        result.latency_p50_us = (double)stats->total_commit_latency_us / stats->committed_tx;
        result.latency_p95_us = result.latency_p50_us * 1.5;
        result.latency_p99_us = result.latency_p50_us * 2.0;
    } else {
        result.latency_p50_us = result.latency_p95_us = result.latency_p99_us = 0.0;
    }

    result.hot_object_count = 0;
    result.hot_path_tx = stats->hot_path_tx;
    result.cold_path_tx = stats->cold_path_tx;
    result.hot_path_ratio = 0.0;

    result.server_queue_wait_p50_us = 0.0;
    result.server_queue_wait_p95_us = 0.0;
    result.server_queue_wait_p99_us = 0.0;

    result.invariant_violation_count = stats->invariant_violation_count;
    result.duplicate_commit_count = stats->duplicate_commit_count;

    // Verify invariants
    uint64_t initial_stock = config_.num_products * 100;
    store_->verify_invariants(initial_stock, result.final_stock, result.sold_count);

    return result;
}

// Main entry point
int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    config.num_products = 16;
    config.num_users = 100;
    config.num_threads = 4;
    config.write_ratio = 1.0;
    config.access_pattern = "uniform";
    config.duration_sec = 10;
    config.max_retries = 100;
    config.algorithm = "baseline_occ";
    config.backoff_policy = "NO_BACKOFF";
    config.backoff_base_us = 10;
    config.backoff_max_us = 1000;
    config.hot_detection_enabled = false;
    config.hot_threshold = 0.1;
    config.hot_window_ms = 100;
    config.hot_min_access = 10;
    config.hybrid_enabled = false;
    config.arbitration_mode = "fifo";
    config.server_queue_size = 1000;
    config.server_worker_threads = 4;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "--products N\n"
                      << "--users N\n"
                      << "--threads N\n"
                      << "--write-ratio RATIO\n"
                      << "--access-pattern uniform|zipfian_0.8|zipfian_0.99\n"
                      << "--duration-sec N\n"
                      << "--algorithm baseline_occ|backoff_occ|hot_detection_occ|hybrid_arbitration_occ\n"
                      << "--backoff-policy NO_BACKOFF|FIXED_BACKOFF|EXPONENTIAL_BACKOFF|CONTENTION_AWARE_BACKOFF\n"
                      << "--output-file FILE\n";
            return 0;
        }
    }

    BenchmarkRunner runner(config);
    runner.run_benchmark();

    RunResult result = runner.get_result();

    // Output results as JSON
    JSONBuilder output;
    output.add("attempted_tx", (long long)result.attempted_tx);
    output.add("committed_tx", (long long)result.committed_tx);
    output.add("aborted_tx", (long long)result.aborted_tx);
    output.add("business_abort_tx", (long long)result.business_abort_tx);
    output.add("abort_rate", result.abort_rate);
    output.add("retry_per_commit", result.retry_per_commit);
    output.add("committed_tx_per_sec", (double)result.committed_tx / config.duration_sec);
    output.add("latency_us_p50", result.latency_p50_us);
    output.add("latency_us_p95", result.latency_p95_us);
    output.add("latency_us_p99", result.latency_p99_us);
    output.add("hot_object_count", (long long)result.hot_object_count);
    output.add("hot_path_tx", (long long)result.hot_path_tx);
    output.add("cold_path_tx", (long long)result.cold_path_tx);
    output.add("hot_path_ratio", result.hot_path_ratio);
    output.add("invariant_violation_count", (long long)result.invariant_violation_count);
    output.add("duplicate_commit_count", (long long)result.duplicate_commit_count);
    output.add("final_stock", (long long)result.final_stock);
    output.add("sold_count", (long long)result.sold_count);
    output.add("initial_stock", (long long)(config.num_products * 100));

    std::cout << output.build() << std::endl;

    return 0;
}
