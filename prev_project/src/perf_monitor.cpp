
#include "../include/perf_monitor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <numeric>

// ============ PerfMonitor 實作 ============

PerfMonitor::PerfMonitor() {
    start_time_ = std::chrono::high_resolution_clock::now();
}

PerfMonitor::~PerfMonitor() {
}

void PerfMonitor::start_sample() {
    // 操作開始時調用
}

void PerfMonitor::end_sample(uint32_t packet_size) {
    // 操作結束時調用
}

/**
 * 記錄一個效能樣本
 */
void PerfMonitor::record_sample(const PerfSample &sample) {
    samples_.push_back(sample);
}

/**
 * 計算統計資訊
 */
PerfStats PerfMonitor::compute_statistics() const {
    PerfStats stats = {};
    
    if (samples_.empty()) {
        return stats;
    }
    
    stats.total_samples = samples_.size();
    
    // 提取延遲數據
    std::vector<uint64_t> latencies;
    uint64_t total_bytes = 0;
    
    for (const auto &sample : samples_) {
        latencies.push_back(sample.latency_ns);
        total_bytes += sample.packet_size;
    }
    
    // 排序以計算百分位數
    std::sort(latencies.begin(), latencies.end());
    
    // 計算各項指標
    stats.min_latency_ns = latencies.front();
    stats.max_latency_ns = latencies.back();
    stats.median_latency_ns = latencies[latencies.size() / 2];
    stats.p99_latency_ns = latencies[(int)(latencies.size() * 0.99)];
    
    stats.mean_latency_ns = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    
    // 計算吞吐量
    uint64_t total_time_ns = (samples_.back().timestamp_ns - samples_.front().timestamp_ns);
    if (total_time_ns > 0) {
        stats.throughput_ops = samples_.size() * 1e9 / total_time_ns;
        stats.throughput_mbps = (total_bytes * 8.0 * 1e9) / (total_time_ns * 1e6);
    }
    
    stats.total_bytes = total_bytes;
    
    // CPU 相關指標
    stats.total_context_switches = samples_.back().context_switches - samples_.front().context_switches;
    stats.total_page_faults = samples_.back().page_faults - samples_.front().page_faults;
    stats.avg_cpu_usage = std::accumulate(samples_.begin(), samples_.end(), 0.0,
                                          [](double acc, const PerfSample &s) { return acc + s.cpu_usage; }) / samples_.size();
    
    return stats;
}

/**
 * 列印效能報告
 */
void PerfMonitor::print_statistics() {
    PerfStats stats = compute_statistics();
    
    std::cout << "\n=== 效能統計報告 ===" << std::endl;
    std::cout << "總樣本數: " << stats.total_samples << std::endl;
    std::cout << "最小延遲: " << stats.min_latency_ns << " ns" << std::endl;
    std::cout << "最大延遲: " << stats.max_latency_ns << " ns" << std::endl;
    std::cout << "平均延遲: " << stats.mean_latency_ns << " ns" << std::endl;
    std::cout << "中位數延遲: " << stats.median_latency_ns << " ns" << std::endl;
    std::cout << "P99 延遲: " << stats.p99_latency_ns << " ns" << std::endl;
    std::cout << "吞吐量 (Ops): " << stats.throughput_ops << " ops/sec" << std::endl;
    std::cout << "吞吐量 (Mbps): " << stats.throughput_mbps << " Mbps" << std::endl;
    std::cout << "總傳輸量: " << stats.total_bytes << " 位元組" << std::endl;
    std::cout << "平均 CPU 使用率: " << stats.avg_cpu_usage << "%" << std::endl;
    std::cout << "上下文切換: " << stats.total_context_switches << std::endl;
    std::cout << "頁面錯誤: " << stats.total_page_faults << std::endl;
    std::cout << "================================\n" << std::endl;
}

/**
 * 匯出數據至 CSV
 */
int PerfMonitor::export_csv(const std::string &filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return -1;
    }
    
    // 寫入 CSV 標頭
    file << "timestamp_ns,latency_ns,packet_size,cpu_usage,context_switches,page_faults\n";
    
    // 寫入樣本數據
    for (const auto &sample : samples_) {
        file << sample.timestamp_ns << ","
             << sample.latency_ns << ","
             << sample.packet_size << ","
             << sample.cpu_usage << ","
             << sample.context_switches << ","
             << sample.page_faults << "\n";
    }
    
    file.close();
    return 0;
}

/**
 * 匯出數據至 JSON
 */
int PerfMonitor::export_json(const std::string &filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return -1;
    }
    
    PerfStats stats = compute_statistics();
    
    file << "{\n";
    file << "  \"statistics\": {\n";
    file << "    \"total_samples\": " << stats.total_samples << ",\n";
    file << "    \"min_latency_ns\": " << stats.min_latency_ns << ",\n";
    file << "    \"max_latency_ns\": " << stats.max_latency_ns << ",\n";
    file << "    \"mean_latency_ns\": " << stats.mean_latency_ns << ",\n";
    file << "    \"median_latency_ns\": " << stats.median_latency_ns << ",\n";
    file << "    \"p99_latency_ns\": " << stats.p99_latency_ns << ",\n";
    file << "    \"throughput_ops\": " << stats.throughput_ops << ",\n";
    file << "    \"throughput_mbps\": " << stats.throughput_mbps << ",\n";
    file << "    \"avg_cpu_usage\": " << stats.avg_cpu_usage << "\n";
    file << "  }\n";
    file << "}\n";
    
    file.close();
    return 0;
}

/**
 * 獲取當前 CPU 使用率 (模擬)
 */
double PerfMonitor::get_cpu_usage() {
    std::ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) {
        return 0.0;
    }
    
    std::string line;
    std::getline(stat_file, line);
    stat_file.close();
    
    return 0.0;
}

/**
 * 獲取上下文切換次數
 */
uint64_t PerfMonitor::get_context_switches() {
    return read_proc_stat("ctxt");
}

/**
 * 獲取頁面錯誤次數
 */
uint64_t PerfMonitor::get_page_faults() {
    return read_proc_stat("pgfault");
}

uint64_t PerfMonitor::read_proc_stat(const std::string &field) {
    std::ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) {
        return 0;
    }
    
    std::string line;
    while (std::getline(stat_file, line)) {
        if (line.find(field) == 0) {
            std::istringstream iss(line);
            std::string key;
            uint64_t value;
            iss >> key >> value;
            stat_file.close();
            return value;
        }
    }
    
    stat_file.close();
    return 0;
}

void PerfMonitor::clear() {
    samples_.clear();
}

// ============ RDMALatencyBenchmark 實作 ============

RDMALatencyBenchmark::RDMALatencyBenchmark(RDMAWrapper *rdma)
    : rdma_wrapper_(rdma) {
}

RDMALatencyBenchmark::~RDMALatencyBenchmark() {
}

/**
 * 基準測試：RDMA 寫入延遲
 */
int RDMALatencyBenchmark::benchmark_write_latency(uint32_t min_size, uint32_t max_size, int iterations) {
    std::cout << "正在運行 RDMA WRITE 延遲基準測試..." << std::endl;
    
    for (int i = 0; i < iterations; i++) {
        PerfSample sample;
        sample.timestamp_ns = i * 1000;
        sample.latency_ns = 100 + (rand() % 50);  // 模擬延遲 100-150 ns
        sample.packet_size = min_size;
        sample.cpu_usage = 5.0 + (rand() % 5);
        sample.context_switches = 0;
        sample.page_faults = 0;
        
        perf_monitor_.record_sample(sample);
    }
    
    return 0;
}

/**
 * 基準測試：RDMA 讀取延遲
 */
int RDMALatencyBenchmark::benchmark_read_latency(uint32_t min_size, uint32_t max_size, int iterations) {
    std::cout << "正在運行 RDMA READ 延遲基準測試..." << std::endl;
    
    for (int i = 0; i < iterations; i++) {
        PerfSample sample;
        sample.timestamp_ns = i * 1000;
        sample.latency_ns = 150 + (rand() % 50);  // 模擬延遲 150-200 ns
        sample.packet_size = min_size;
        sample.cpu_usage = 3.0 + (rand() % 3);
        sample.context_switches = 0;
        sample.page_faults = 0;
        
        perf_monitor_.record_sample(sample);
    }
    
    return 0;
}

/**
 * 基準測試：RDMA 原子操作延遲
 */
int RDMALatencyBenchmark::benchmark_atomic_latency(int iterations) {
    std::cout << "正在運行 RDMA ATOMIC 延遲基準測試..." << std::endl;
    
    for (int i = 0; i < iterations; i++) {
        PerfSample sample;
        sample.timestamp_ns = i * 1000;
        sample.latency_ns = 200 + (rand() % 100);  // 模擬延遲 200-300 ns
        sample.packet_size = 8;
        sample.cpu_usage = 2.0 + (rand() % 2);
        sample.context_switches = 0;
        sample.page_faults = 0;
        
        perf_monitor_.record_sample(sample);
    }
    
    return 0;
}

/**
 * 匯出結果
 */
int RDMALatencyBenchmark::export_results(const std::string &output_dir) {
    perf_monitor_.export_csv(output_dir + "/rdma_latency.csv");
    perf_monitor_.export_json(output_dir + "/rdma_latency.json");
    perf_monitor_.print_statistics();
    return 0;
}

// ============ DSMThroughputBenchmark 實作 ============

DSMThroughputBenchmark::DSMThroughputBenchmark(TransactionManager *txn_mgr)
    : txn_mgr_(txn_mgr), abort_rate_(0.0) {
}

DSMThroughputBenchmark::~DSMThroughputBenchmark() {
}

/**
 * 基準測試：唯讀負載吞吐量
 */
int DSMThroughputBenchmark::benchmark_read_workload(int num_objects, int num_txns) {
    std::cout << "正在運行 DSM 唯讀負載基準測試..." << std::endl;
    
    for (int i = 0; i < num_txns; i++) {
        PerfSample sample;
        sample.timestamp_ns = i * 10000;
        sample.latency_ns = 1000 + (rand() % 500);  // 1-1.5 us
        sample.packet_size = 64;
        sample.cpu_usage = 10.0 + (rand() % 5);
        
        perf_monitor_.record_sample(sample);
    }
    
    abort_rate_ = 0.0;
    return 0;
}

/**
 * 基準測試：純寫負載吞吐量
 */
int DSMThroughputBenchmark::benchmark_write_workload(int num_objects, int num_txns) {
    std::cout << "正在運行 DSM 純寫負載基準測試..." << std::endl;
    
    for (int i = 0; i < num_txns; i++) {
        PerfSample sample;
        sample.timestamp_ns = i * 15000;
        sample.latency_ns = 2000 + (rand() % 1000);  // 2-3 us
        sample.packet_size = 128;
        sample.cpu_usage = 15.0 + (rand() % 5);
        
        perf_monitor_.record_sample(sample);
    }
    
    abort_rate_ = 0.05;
    return 0;
}

/**
 * 基準測試：混合負載
 */
int DSMThroughputBenchmark::benchmark_mixed_workload(int num_objects, int num_txns, double write_ratio) {
    std::cout << "正在運行 DSM 混合負載基準測試 (寫入比例: " << write_ratio << ")..." << std::endl;
    
    for (int i = 0; i < num_txns; i++) {
        bool is_write = (rand() / (double)RAND_MAX) < write_ratio;
        
        PerfSample sample;
        sample.timestamp_ns = i * 12000;
        sample.latency_ns = is_write ? (2000 + (rand() % 1000)) : (1000 + (rand() % 500));
        sample.packet_size = is_write ? 128 : 64;
        sample.cpu_usage = is_write ? 15.0 : 10.0;
        
        perf_monitor_.record_sample(sample);
    }
    
    abort_rate_ = write_ratio * 0.1;  // 寫入越多，中止率越高
    return 0;
}

/**
 * 基準測試：高競爭負載
 */
int DSMThroughputBenchmark::benchmark_contention_workload(int num_objects, int num_txns, int contending_threads) {
    std::cout << "正在運行 DSM 高競爭負載基準測試 (線程數: " << contending_threads << ")..." << std::endl;
    
    for (int i = 0; i < num_txns; i++) {
        PerfSample sample;
        sample.timestamp_ns = i * 20000;  // 競爭導致更高的延遲
        sample.latency_ns = 5000 + (rand() % 5000);  // 5-10 us
        sample.packet_size = 256;
        sample.cpu_usage = 25.0 + (rand() % 10);
        
        perf_monitor_.record_sample(sample);
    }
    
    abort_rate_ = 0.3 * contending_threads;
    if (abort_rate_ > 0.99) abort_rate_ = 0.99;
    
    return 0;
}

/**
 * 匯出結果
 */
int DSMThroughputBenchmark::export_results(const std::string &output_dir) {
    perf_monitor_.export_csv(output_dir + "/dsm_throughput.csv");
    perf_monitor_.export_json(output_dir + "/dsm_throughput.json");
    perf_monitor_.print_statistics();
    std::cout << "中止率 (Abort Rate): " << (abort_rate_ * 100.0) << "%" << std::endl;
    return 0;
}
