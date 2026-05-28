/**
 * 效能監控與基準測試模組
 * 用於測量延遲、吞吐量以及作業系統層級的指標
 * 
 */

#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <cstdint>
#include <vector>
#include <memory>
#include <chrono>
#include <string>
#include <fstream>

/**
 * 效能樣本數據
 */
struct PerfSample {
    uint64_t timestamp_ns;      // 時間戳 (納秒)
    uint64_t latency_ns;        // 延遲 (納秒)
    uint32_t packet_size;       // 封包大小
    double cpu_usage;           // CPU 使用率
    uint64_t context_switches;  // 上下文切換次數
    uint64_t page_faults;       // 頁面錯誤次數
};

/**
 * 效能統計結果
 */
struct PerfStats {
    uint64_t total_samples;     // 總樣本數
    uint64_t min_latency_ns;    // 最小延遲
    uint64_t max_latency_ns;    // 最大延遲
    double mean_latency_ns;     // 平均延遲
    double median_latency_ns;   // 中位數延遲
    double p99_latency_ns;      // 99百分位延遲 (P99)
    
    uint64_t throughput_ops;    // 吞吐量 (每秒操作數)
    uint64_t total_bytes;       // 總傳輸位元組
    double throughput_mbps;     // 吞吐量 (Mbps)
    
    double avg_cpu_usage;             // 平均 CPU 使用率
    uint64_t total_context_switches;  // 總上下文切換次數
    uint64_t total_page_faults;       // 總頁面錯誤次數
};

/**
 * 效能監控器
 */
class PerfMonitor {
public:
    PerfMonitor();
    ~PerfMonitor();
    
    // 樣本採集
    void start_sample();
    void end_sample(uint32_t packet_size = 0);
    void record_sample(const PerfSample &sample);
    
    // 統計計算
    PerfStats compute_statistics() const;
    void print_statistics();
    
    // 數據匯出
    int export_csv(const std::string &filename);
    int export_json(const std::string &filename);
    
    // OS 指標獲取
    double get_cpu_usage();
    uint64_t get_context_switches();
    uint64_t get_page_faults();
    
    // 工具函數
    void clear();
    size_t get_sample_count() const { return samples_.size(); }
    
private:
    std::vector<PerfSample> samples_;
    std::chrono::high_resolution_clock::time_point start_time_;
    
    // 用於追蹤增量指標
    uint64_t last_context_switches_ = 0;
    uint64_t last_page_faults_ = 0;
    
    uint64_t read_proc_stat(const std::string &field);
};

/**
 * 微基準測試 - RDMA 操作延遲
 */
class RDMALatencyBenchmark {
public:
    RDMALatencyBenchmark(class RDMAWrapper *rdma);
    ~RDMALatencyBenchmark();
    
    // 基準測試項目
    int benchmark_write_latency(uint32_t min_size, uint32_t max_size, int iterations = 1000); // 寫入延遲
    int benchmark_read_latency(uint32_t min_size, uint32_t max_size, int iterations = 1000);  // 讀取延遲
    int benchmark_atomic_latency(int iterations = 1000);                                     // 原子操作延遲
    
    // 結果獲取
    PerfStats get_statistics() const { return perf_monitor_.compute_statistics(); }
    int export_results(const std::string &output_dir);
    
private:
    class RDMAWrapper *rdma_wrapper_;
    PerfMonitor perf_monitor_;
};

/**
 * 宏基準測試 - DSM 事務吞吐量
 */
class DSMThroughputBenchmark {
public:
    DSMThroughputBenchmark(class TransactionManager *txn_mgr);
    ~DSMThroughputBenchmark();
    
    // 測試場景
    int benchmark_read_workload(int num_objects, int num_txns);                     // 唯讀負載
    int benchmark_write_workload(int num_objects, int num_txns);                    // 純寫負載
    int benchmark_mixed_workload(int num_objects, int num_txns, double write_ratio = 0.5); // 混合負載
    int benchmark_contention_workload(int num_objects, int num_txns, int contending_threads); // 高競爭負載
    
    // 結果獲取
    PerfStats get_statistics() const { return perf_monitor_.compute_statistics(); }
    double get_abort_rate() const { return abort_rate_; }
    int export_results(const std::string &output_dir);
    
private:
    class TransactionManager *txn_mgr_;
    PerfMonitor perf_monitor_;
    double abort_rate_;
};

#endif // PERF_MONITOR_H
