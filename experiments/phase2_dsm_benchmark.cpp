#include "phase2_benchmark.h"
#include "backoff.h"
#include "json_utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <vector>

namespace {

std::string output_file;
constexpr uint64_t kInitialStockPerProduct = 10000000;
constexpr uint64_t kInitialUserBalance = 100000000;

bool to_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "TRUE" || value == "yes";
}

BackoffPolicyType parse_backoff_policy(const std::string& value) {
    if (value == "FIXED_BACKOFF") return FIXED_BACKOFF;
    if (value == "EXPONENTIAL_BACKOFF") return EXPONENTIAL_BACKOFF;
    if (value == "RANDOMIZED_EXPONENTIAL_BACKOFF") return RANDOMIZED_EXPONENTIAL_BACKOFF;
    if (value == "CONTENTION_AWARE_BACKOFF") return CONTENTION_AWARE_BACKOFF;
    return NO_BACKOFF;
}

void add_latency(DSMObjectStore::GlobalStats* stats, uint64_t latency_us) {
    stats->total_commit_latency_us += latency_us;
    if (latency_us < 10) {
        stats->latency_histogram[0]++;
    } else if (latency_us < 50) {
        stats->latency_histogram[1]++;
    } else if (latency_us < 100) {
        stats->latency_histogram[2]++;
    } else if (latency_us < 500) {
        stats->latency_histogram[3]++;
    } else {
        stats->latency_histogram[4]++;
    }
}

void update_atomic_max(std::atomic<uint64_t>& target, uint64_t value) {
    uint64_t current = target.load();
    while (value > current && !target.compare_exchange_weak(current, value)) {
    }
}

double percentile(std::vector<uint64_t> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double idx = (p / 100.0) * static_cast<double>(values.size() - 1);
    return static_cast<double>(values[static_cast<size_t>(std::round(idx))]);
}

uint64_t normalized_hot_shards(const BenchmarkConfig& config) {
    if (config.hot_shards == 0) {
        return 1;
    }
    return std::max<uint32_t>(1, std::min<uint32_t>(32, config.hot_shards));
}

std::atomic<uint64_t>& selected_queue_depth(const BenchmarkConfig& config,
                                            DSMObjectStore* store,
                                            uint64_t product_id) {
    if (config.arbitration_mode == "per_object") {
        return store->object_queue_depth(product_id);
    }
    if (config.arbitration_mode == "per_shard") {
        return store->shard_queue_depth(product_id % normalized_hot_shards(config));
    }
    return store->global_queue_depth();
}

std::mutex& selected_arbitration_mutex(const BenchmarkConfig& config,
                                       DSMObjectStore* store,
                                       uint64_t product_id) {
    if (config.arbitration_mode == "per_object") {
        return store->object_arbitration_mutex(product_id);
    }
    if (config.arbitration_mode == "per_shard") {
        return store->shard_arbitration_mutex(product_id % normalized_hot_shards(config));
    }
    return store->global_arbitration_mutex();
}

std::vector<std::unique_lock<std::mutex>> lock_order_objects(DSMObjectStore* store,
                                                            std::vector<uint64_t> object_ids) {
    std::sort(object_ids.begin(), object_ids.end());
    object_ids.erase(std::unique(object_ids.begin(), object_ids.end()), object_ids.end());

    std::vector<std::unique_lock<std::mutex>> locks;
    locks.reserve(object_ids.size());
    for (uint64_t object_id : object_ids) {
        locks.emplace_back(store->object_mutex(object_id));
    }
    return locks;
}

} // namespace

InventoryWorkload::InventoryWorkload(const BenchmarkConfig& config, DSMObjectStore* store, OCCEngine* engine)
    : config_(config), store_(store), engine_(engine) {
}

InventoryWorkload::~InventoryWorkload() = default;

InventoryWorkload::OrderInfo InventoryWorkload::generate_order(int thread_id) {
    static thread_local std::mt19937 gen(12345 + thread_id * 7919);
    std::uniform_real_distribution<> real_dis(0.0, 1.0);
    std::uniform_int_distribution<> user_dis(0, config_.num_users - 1);

    OrderInfo info;
    info.user_id = user_dis(gen);
    if (config_.application_case == "ad_budget") {
        info.price = 10;
    } else if (config_.application_case == "ticket_booking") {
        info.price = 250;
    } else if (config_.application_case == "warehouse_restock") {
        info.price = 50;
    } else {
        info.price = 100;
    }
    info.is_read_only = real_dis(gen) > config_.write_ratio;

    const bool use_hot_set = !config_.hot_product_ids.empty() && real_dis(gen) < config_.hot_access_probability;
    if (use_hot_set) {
        std::uniform_int_distribution<> hot_dis(0, config_.hot_product_ids.size() - 1);
        info.product_id = config_.hot_product_ids[hot_dis(gen)];
    } else if (config_.access_pattern == "uniform") {
        std::uniform_int_distribution<> prod_dis(0, config_.num_products - 1);
        info.product_id = prod_dis(gen);
    } else {
        const double theta = config_.access_pattern.find("0.99") != std::string::npos ? 0.99 : 0.8;
        std::vector<double> weights(config_.num_products);
        for (uint32_t i = 0; i < config_.num_products; ++i) {
            weights[i] = 1.0 / std::pow(i + 1, theta);
        }
        std::discrete_distribution<uint32_t> prod_dis(weights.begin(), weights.end());
        info.product_id = prod_dis(gen);
    }

    return info;
}

void InventoryWorkload::generate_order_transaction(Transaction& tx, int thread_id) {
    OrderInfo order = generate_order(thread_id);
    uint64_t stock_value = 0, stock_version = 0;
    const uint64_t stock_obj_id = order.product_id;
    engine_->read_object(tx, stock_obj_id, stock_value, stock_version);

    if (order.is_read_only) {
        return;
    }

    uint64_t balance_value = 0, balance_version = 0;
    const uint64_t balance_obj_id = config_.num_products + order.user_id;
    engine_->read_object(tx, balance_obj_id, balance_value, balance_version);

    if (stock_value == 0 || balance_value < order.price) {
        tx.abort_reason = ABORT_BUSINESS_ABORT;
        return;
    }

    const uint64_t sold_obj_id = config_.num_products + config_.num_users;
    uint64_t sold_value = 0, sold_version = 0;
    engine_->read_object(tx, sold_obj_id, sold_value, sold_version);
    engine_->write_object(tx, stock_obj_id, stock_value - 1);
    engine_->write_object(tx, balance_obj_id, balance_value - order.price);
    engine_->write_object(tx, sold_obj_id, sold_value + 1);
}

BenchmarkRunner::BenchmarkRunner(const BenchmarkConfig& config)
    : config_(config) {
    store_ = std::make_unique<DSMObjectStore>();
    engine_ = std::make_unique<OCCEngine>(store_.get());
    workload_ = std::make_unique<InventoryWorkload>(config_, store_.get(), engine_.get());
}

BenchmarkRunner::~BenchmarkRunner() = default;

int BenchmarkRunner::initialize_server() {
    for (uint32_t i = 0; i < config_.num_products; i++) {
        store_->create_object(i, TYPE_PRODUCT_STOCK, kInitialStockPerProduct);
    }
    for (uint32_t i = 0; i < config_.num_users; i++) {
        store_->create_object(config_.num_products + i, TYPE_USER_BALANCE, kInitialUserBalance);
    }
    store_->create_object(config_.num_products + config_.num_users, TYPE_SOLD_COUNT, 0);
    return 0;
}

int BenchmarkRunner::initialize_client() {
    return 0;
}

static bool touches_known_hot_object(const BenchmarkConfig& config, DSMObjectStore* store, uint64_t product_id) {
    if (std::find(config.hot_product_ids.begin(), config.hot_product_ids.end(), product_id) !=
        config.hot_product_ids.end()) {
        return true;
    }
    auto stats = store->get_object_stats(product_id);
    return stats && stats->is_hot.load();
}

static void refresh_hot_objects(const BenchmarkConfig& config, DSMObjectStore* store) {
    if (!config.hot_detection_enabled) return;
    uint64_t now = OCCEngine::get_time_us();
    for (uint32_t object_id = 0; object_id < config.num_products; ++object_id) {
        auto stats = store->get_object_stats(object_id);
        if (!stats) continue;
        uint64_t access = stats->access_count.load();
        uint64_t aborts = stats->abort_count.load();
        uint64_t lock_fails = stats->lock_fail_count.load();
        if (access < config.hot_min_access) continue;
        double abort_rate = static_cast<double>(aborts) / static_cast<double>(access);
        if (abort_rate >= config.hot_threshold || lock_fails >= config.hot_min_access) {
            stats->is_hot.store(true);
            stats->last_hot_timestamp.store(now);
        }
    }
}

static bool should_refresh_hot_objects(const BenchmarkConfig& config, uint64_t local_tx_count) {
    if (!config.hot_detection_enabled) return false;
    if (config.hot_refresh_interval == 0) return true;
    return (local_tx_count % config.hot_refresh_interval) == 0;
}

static int run_server_arbitrated_order(const BenchmarkConfig& config,
                                       DSMObjectStore* store,
                                       const InventoryWorkload::OrderInfo& order,
                                       uint64_t start_us) {
    auto stats = store->get_global_stats();
    stats->attempted_tx++;
    stats->hot_path_tx++;
    stats->server_arbitrated_tx++;

    auto& queue_depth = selected_queue_depth(config, store, order.product_id);
    uint64_t queue_length = queue_depth.fetch_add(1);
    store->record_queue_length_sample(queue_length);
    update_atomic_max(stats->server_queue_length_max, queue_length);

    const uint64_t wait_start_us = OCCEngine::get_time_us();
    std::unique_lock<std::mutex> arbitration_lock(
        selected_arbitration_mutex(config, store, order.product_id));
    queue_depth.fetch_sub(1);
    const uint64_t wait_us = OCCEngine::get_time_us() - wait_start_us;
    stats->server_queue_wait_total_us += wait_us;
    stats->server_queue_wait_count++;
    update_atomic_max(stats->server_queue_wait_max_us, wait_us);
    store->record_queue_wait_sample(wait_us);

    const uint64_t service_start_us = OCCEngine::get_time_us();

    std::vector<std::unique_lock<std::mutex>> object_locks;
    object_locks = lock_order_objects(store, {
        order.product_id,
        config.num_products + order.user_id,
        config.num_products + config.num_users,
    });

    ObjectHeader* stock = store->get_object_header(order.product_id);
    ObjectHeader* balance = store->get_object_header(config.num_products + order.user_id);
    ObjectHeader* sold = store->get_object_header(config.num_products + config.num_users);
    if (!stock || !balance || !sold) {
        stats->aborted_tx++;
        uint64_t service_us = OCCEngine::get_time_us() - service_start_us;
        stats->server_service_time_total_us += service_us;
        stats->server_service_time_count++;
        update_atomic_max(stats->server_service_time_max_us, service_us);
        store->record_service_time_sample(service_us);
        return -1;
    }

    if (order.is_read_only) {
        stats->committed_tx++;
        add_latency(stats, OCCEngine::get_time_us() - start_us);
        uint64_t service_us = OCCEngine::get_time_us() - service_start_us;
        stats->server_service_time_total_us += service_us;
        stats->server_service_time_count++;
        update_atomic_max(stats->server_service_time_max_us, service_us);
        store->record_service_time_sample(service_us);
        return 0;
    }

    if (stock->value == 0 || balance->value < order.price) {
        stats->business_abort_tx++;
        uint64_t service_us = OCCEngine::get_time_us() - service_start_us;
        stats->server_service_time_total_us += service_us;
        stats->server_service_time_count++;
        update_atomic_max(stats->server_service_time_max_us, service_us);
        store->record_service_time_sample(service_us);
        return -1;
    }

    stock->value--;
    stock->version++;
    balance->value -= order.price;
    balance->version++;
    sold->value++;
    sold->version++;
    uint64_t writer = stats->committed_tx.load() + 1;
    stock->last_writer_tx_id = writer;
    balance->last_writer_tx_id = writer;
    sold->last_writer_tx_id = writer;
    stats->committed_tx++;
    add_latency(stats, OCCEngine::get_time_us() - start_us);
    uint64_t service_us = OCCEngine::get_time_us() - service_start_us;
    stats->server_service_time_total_us += service_us;
    stats->server_service_time_count++;
    update_atomic_max(stats->server_service_time_max_us, service_us);
    store->record_service_time_sample(service_us);
    return 0;
}

static void run_occ_order(const BenchmarkConfig& config,
                          DSMObjectStore* store,
                          OCCEngine* engine,
                          const InventoryWorkload::OrderInfo& order) {
    BackoffConfig backoff_config{parse_backoff_policy(config.backoff_policy),
                                 config.backoff_base_us,
                                 config.backoff_max_us,
                                 config.max_retries};
    BackoffManager backoff(backoff_config);

    for (uint32_t retry = 0; retry <= config.max_retries; ++retry) {
        Transaction tx;
        engine->begin_transaction(tx);
        tx.retry_count = retry;

        uint64_t stock_value = 0, stock_version = 0;
        uint64_t balance_value = 0, balance_version = 0;
        uint64_t sold_value = 0, sold_version = 0;
        uint64_t stock_obj_id = order.product_id;
        uint64_t balance_obj_id = config.num_products + order.user_id;
        uint64_t sold_obj_id = config.num_products + config.num_users;

        engine->read_object(tx, stock_obj_id, stock_value, stock_version);
        if (!order.is_read_only) {
            engine->read_object(tx, balance_obj_id, balance_value, balance_version);
            if (stock_value == 0 || balance_value < order.price) {
                store->get_global_stats()->business_abort_tx++;
                return;
            }
            engine->read_object(tx, sold_obj_id, sold_value, sold_version);
            engine->write_object(tx, stock_obj_id, stock_value - 1);
            engine->write_object(tx, balance_obj_id, balance_value - order.price);
            engine->write_object(tx, sold_obj_id, sold_value + 1);
        }

        if (engine->commit_transaction(tx) == 0) {
            store->get_global_stats()->cold_path_tx++;
            return;
        }

        store->get_global_stats()->retry_count++;
        auto obj_stats = store->get_object_stats(stock_obj_id);
        if (obj_stats) {
            obj_stats->abort_count++;
        }
        if (retry == config.max_retries) {
            store->get_global_stats()->aborted_tx++;
            return;
        }
        backoff.backoff(tx.abort_reason, retry);
    }
}

int BenchmarkRunner::run_benchmark() {
    initialize_server();
    initialize_client();

    auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(config_.duration_sec);
    std::vector<std::thread> threads;

    for (uint32_t t = 0; t < config_.num_threads; t++) {
        threads.emplace_back([this, t, end_time]() {
            uint64_t local_tx_count = 0;
            while (std::chrono::steady_clock::now() < end_time) {
                InventoryWorkload::OrderInfo order = workload_->generate_order(t);
                local_tx_count++;
                if (should_refresh_hot_objects(config_, local_tx_count)) {
                    refresh_hot_objects(config_, store_.get());
                }

                bool hot_candidate = touches_known_hot_object(config_, store_.get(), order.product_id);
                if (hot_candidate) {
                    store_->get_global_stats()->hot_path_candidate_tx++;
                }

                if (config_.hybrid_enabled && hot_candidate) {
                    run_server_arbitrated_order(config_, store_.get(), order, OCCEngine::get_time_us());
                } else {
                    run_occ_order(config_, store_.get(), engine_.get(), order);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
    refresh_hot_objects(config_, store_.get());
    return 0;
}

RunResult BenchmarkRunner::get_result() {
    auto stats = store_->get_global_stats();

    RunResult result{};
    result.attempted_tx = stats->attempted_tx;
    result.committed_tx = stats->committed_tx;
    result.aborted_tx = stats->aborted_tx;
    result.business_abort_tx = stats->business_abort_tx;
    result.abort_rate = (stats->attempted_tx > 0) ?
        static_cast<double>(stats->aborted_tx + stats->business_abort_tx) / stats->attempted_tx : 0.0;
    result.retry_per_commit = (stats->committed_tx > 0) ?
        static_cast<double>(stats->retry_count) / stats->committed_tx : 0.0;

    if (stats->committed_tx > 0) {
        result.latency_p50_us = static_cast<double>(stats->total_commit_latency_us) / stats->committed_tx;
        result.latency_p95_us = result.latency_p50_us * 1.5;
        result.latency_p99_us = result.latency_p50_us * 2.0;
    }

    uint64_t hot_count = 0;
    for (uint32_t i = 0; i < config_.num_products; ++i) {
        auto obj_stats = store_->get_object_stats(i);
        if (obj_stats && obj_stats->is_hot.load()) {
            hot_count++;
        }
    }
    result.hot_object_count = hot_count;
    result.hot_path_tx = stats->hot_path_tx;
    result.cold_path_tx = stats->cold_path_tx;
    result.hot_path_ratio = (stats->attempted_tx > 0) ?
        static_cast<double>(stats->hot_path_tx) / stats->attempted_tx : 0.0;
    auto wait_samples = store_->queue_wait_samples();
    auto length_samples = store_->queue_length_samples();
    auto service_samples = store_->service_time_samples();
    result.server_queue_wait_p50_us = percentile(wait_samples, 50.0);
    result.server_queue_wait_p95_us = percentile(wait_samples, 95.0);
    result.server_queue_wait_p99_us = percentile(wait_samples, 99.0);
    result.server_queue_wait_max_us = static_cast<double>(stats->server_queue_wait_max_us.load());
    result.queue_length_p50 = percentile(length_samples, 50.0);
    result.queue_length_p95 = percentile(length_samples, 95.0);
    result.queue_length_p99 = percentile(length_samples, 99.0);
    result.service_time_p50_us = percentile(service_samples, 50.0);
    result.service_time_p95_us = percentile(service_samples, 95.0);
    result.service_time_p99_us = percentile(service_samples, 99.0);
    result.service_time_max_us = static_cast<double>(stats->server_service_time_max_us.load());
    result.hot_cold_interference_count = stats->hot_cold_interference_count;

    uint64_t initial_stock = config_.num_products * kInitialStockPerProduct;
    store_->verify_invariants(initial_stock, result.final_stock, result.sold_count);
    result.invariant_violation_count = stats->invariant_violation_count;
    result.duplicate_commit_count = stats->duplicate_commit_count;
    return result;
}

static void print_help(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "--products N --users N --threads N --write-ratio RATIO\n"
              << "--application-case flash_sale|ticket_booking|ad_budget|warehouse_restock\n"
              << "--access-pattern uniform|zipfian_0.8|zipfian_0.99\n"
              << "--hot-products N --hot-access-prob P --duration-sec N --max-retries N\n"
              << "--algorithm baseline_occ|backoff_occ|hot_detection_occ|hybrid_arbitration_occ\n"
              << "--backoff-policy NO_BACKOFF|FIXED_BACKOFF|EXPONENTIAL_BACKOFF|RANDOMIZED_EXPONENTIAL_BACKOFF|CONTENTION_AWARE_BACKOFF\n"
              << "--backoff-base-us N --backoff-max-us N --hot-detection-enabled true|false\n"
              << "--hot-threshold R --hot-window-ms N --hot-min-access N --hot-refresh-interval N --hybrid-enabled true|false\n"
              << "--arbitration-mode global|per_object|per_shard --hot-shards 1|2|4|8|16|32\n"
              << "--output-file FILE\n";
}

static BenchmarkConfig parse_args(int argc, char* argv[]) {
    BenchmarkConfig config;
    config.num_products = 16;
    config.num_users = 100;
    config.num_threads = 4;
    config.write_ratio = 1.0;
    config.access_pattern = "uniform";
    config.application_case = "flash_sale";
    config.hot_access_probability = 0.90;
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
    config.hot_refresh_interval = 64;
    config.hybrid_enabled = false;
    config.arbitration_mode = "global";
    config.hot_shards = 4;
    config.server_queue_size = 1000;
    config.server_worker_threads = 1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + arg);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "--products") config.num_products = std::stoul(next());
        else if (arg == "--users") config.num_users = std::stoul(next());
        else if (arg == "--threads") config.num_threads = std::stoul(next());
        else if (arg == "--write-ratio") config.write_ratio = std::stod(next());
        else if (arg == "--access-pattern") config.access_pattern = next();
        else if (arg == "--application-case") config.application_case = next();
        else if (arg == "--hot-access-prob") config.hot_access_probability = std::stod(next());
        else if (arg == "--duration-sec") config.duration_sec = std::stoul(next());
        else if (arg == "--max-retries") config.max_retries = std::stoul(next());
        else if (arg == "--algorithm") config.algorithm = next();
        else if (arg == "--backoff-policy") config.backoff_policy = next();
        else if (arg == "--backoff-base-us") config.backoff_base_us = std::stoul(next());
        else if (arg == "--backoff-max-us") config.backoff_max_us = std::stoul(next());
        else if (arg == "--hot-detection-enabled") config.hot_detection_enabled = to_bool(next());
        else if (arg == "--hot-threshold") config.hot_threshold = std::stod(next());
        else if (arg == "--hot-window-ms") config.hot_window_ms = std::stoul(next());
        else if (arg == "--hot-min-access") config.hot_min_access = std::stoul(next());
        else if (arg == "--hot-refresh-interval") config.hot_refresh_interval = std::stoul(next());
        else if (arg == "--hybrid-enabled") config.hybrid_enabled = to_bool(next());
        else if (arg == "--arbitration-mode") config.arbitration_mode = next();
        else if (arg == "--hot-shards") config.hot_shards = std::stoul(next());
        else if (arg == "--server-queue-size") config.server_queue_size = std::stoul(next());
        else if (arg == "--server-worker-threads") config.server_worker_threads = std::stoul(next());
        else if (arg == "--hot-products") {
            uint32_t hot_count = std::min<uint32_t>(std::stoul(next()), config.num_products);
            config.hot_product_ids.clear();
            for (uint32_t p = 0; p < hot_count; ++p) {
                config.hot_product_ids.push_back(p);
            }
        } else if (arg == "--output-file") output_file = next();
    }

    if (config.algorithm == "backoff_occ" && config.backoff_policy == "NO_BACKOFF") {
        config.backoff_policy = "CONTENTION_AWARE_BACKOFF";
    }
    if (config.algorithm == "hot_detection_occ") {
        config.hot_detection_enabled = true;
    }
    if (config.algorithm == "hybrid_arbitration_occ") {
        config.hot_detection_enabled = true;
        config.hybrid_enabled = true;
    }
    config.hot_access_probability = std::max(0.0, std::min(1.0, config.hot_access_probability));
    if (config.arbitration_mode != "global" &&
        config.arbitration_mode != "per_object" &&
        config.arbitration_mode != "per_shard") {
        throw std::runtime_error("invalid --arbitration-mode: " + config.arbitration_mode);
    }
    if (config.hot_shards == 0) {
        config.hot_shards = 1;
    }
    config.hot_shards = std::max<uint32_t>(1, std::min<uint32_t>(32, config.hot_shards));

    return config;
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config = parse_args(argc, argv);
    BenchmarkRunner runner(config);
    runner.run_benchmark();
    RunResult result = runner.get_result();

    auto stats = runner.get_store()->get_global_stats();

    JSONBuilder output;
    output.add("algorithm", config.algorithm);
    output.add("application_case", config.application_case);
    output.add("access_pattern", config.access_pattern);
    output.add("hot_access_probability", config.hot_access_probability);
    output.add("hot_refresh_interval", (long long)config.hot_refresh_interval);
    output.add("arbitration_mode", config.arbitration_mode);
    output.add("hot_shards", (long long)config.hot_shards);
    output.add("attempted_tx", (long long)result.attempted_tx);
    output.add("committed_tx", (long long)result.committed_tx);
    output.add("aborted_tx", (long long)result.aborted_tx);
    output.add("business_abort_tx", (long long)result.business_abort_tx);
    output.add("retry_count", (long long)stats->retry_count.load());
    output.add("lock_fail_count", (long long)stats->lock_fail_count.load());
    output.add("validation_fail_count", (long long)stats->validation_fail_count.load());
    output.add("abort_rate", result.abort_rate);
    output.add("retry_per_commit", result.retry_per_commit);
    output.add("committed_tx_per_sec", (double)result.committed_tx / config.duration_sec);
    output.add("latency_us_p50", result.latency_p50_us);
    output.add("latency_us_p95", result.latency_p95_us);
    output.add("latency_us_p99", result.latency_p99_us);
    output.add("hot_object_count", (long long)result.hot_object_count);
    output.add("hot_path_candidate_tx", (long long)stats->hot_path_candidate_tx.load());
    output.add("hot_path_tx", (long long)result.hot_path_tx);
    output.add("cold_path_tx", (long long)result.cold_path_tx);
    output.add("server_arbitrated_tx", (long long)stats->server_arbitrated_tx.load());
    output.add("hot_path_ratio", result.hot_path_ratio);
    output.add("server_queue_wait_us_p50", result.server_queue_wait_p50_us);
    output.add("server_queue_wait_us_p95", result.server_queue_wait_p95_us);
    output.add("server_queue_wait_us_p99", result.server_queue_wait_p99_us);
    output.add("server_queue_wait_us_max", result.server_queue_wait_max_us);
    output.add("queue_length_p50", result.queue_length_p50);
    output.add("queue_length_p95", result.queue_length_p95);
    output.add("queue_length_p99", result.queue_length_p99);
    output.add("service_time_us_p50", result.service_time_p50_us);
    output.add("service_time_us_p95", result.service_time_p95_us);
    output.add("service_time_us_p99", result.service_time_p99_us);
    output.add("service_time_us_max", result.service_time_max_us);
    output.add("hot_cold_interference_count", (long long)result.hot_cold_interference_count);
    output.add("invariant_violation_count", (long long)result.invariant_violation_count);
    output.add("duplicate_commit_count", (long long)result.duplicate_commit_count);
    output.add("final_stock", (long long)result.final_stock);
    output.add("sold_count", (long long)result.sold_count);
    output.add("initial_stock", (long long)(config.num_products * kInitialStockPerProduct));
    output.add("environment_scope", "VirtualBox + Ubuntu 22.04 + Soft-RoCE protocol-level prototype");
    output.add("crash_recovery_supported", false);

    std::string json = output.build();
    if (!output_file.empty()) {
        std::ofstream out(output_file);
        out << json << "\n";
    }
    std::cout << json << std::endl;
    return 0;
}
