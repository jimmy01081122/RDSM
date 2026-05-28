#ifndef LATENCY_SAMPLER_H
#define LATENCY_SAMPLER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

enum class LatencySamplingMode {
    Off,
    Full,
    Reservoir,
};

struct LatencySample {
    uint64_t tx_id{0};
    uint32_t thread_id{0};
    std::string_view workload_name;
    std::string_view application_case;
    std::string_view algorithm;
    std::string_view arbitration_mode;
    uint32_t hot_shards{0};
    std::string_view sold_counter_mode;
    std::string_view path_type;
    bool appendix_only{false};
    std::string_view appendix_reason;
    uint64_t tx_start_ns{0};
    uint64_t read_phase_done_ns{0};
    uint64_t route_decision_ns{0};
    uint64_t queue_enter_ns{0};
    uint64_t queue_leave_ns{0};
    uint64_t commit_start_ns{0};
    uint64_t commit_done_ns{0};
    uint64_t tx_end_ns{0};
    uint32_t retry_count{0};
    uint32_t lock_fail_count_for_tx{0};
    uint32_t validation_fail_count_for_tx{0};
    std::string_view final_status;
    uint32_t objects_touched{0};
    uint32_t hot_objects_touched{0};
};

struct LatencySummary {
    double tx_latency_us_p50{0};
    double tx_latency_us_p95{0};
    double tx_latency_us_p99{0};
    double tx_latency_us_max{0};
    double committed_tx_latency_us_p50{0};
    double committed_tx_latency_us_p95{0};
    double committed_tx_latency_us_p99{0};
    double abort_tx_latency_us_p50{0};
    double abort_tx_latency_us_p95{0};
    double abort_tx_latency_us_p99{0};
    double cold_occ_latency_us_p50{0};
    double cold_occ_latency_us_p95{0};
    double cold_occ_latency_us_p99{0};
    double hot_arbitration_latency_us_p50{0};
    double hot_arbitration_latency_us_p95{0};
    double hot_arbitration_latency_us_p99{0};
    double retry_count_p50{0};
    double retry_count_p95{0};
    double retry_count_p99{0};
    uint64_t latency_sample_count{0};
};

class LatencySampler {
public:
    LatencySampler(LatencySamplingMode mode, size_t sample_size);

    uint64_t next_tx_id();
    bool enabled() const;
    LatencySamplingMode mode() const { return mode_; }
    size_t sample_size() const { return sample_size_; }
    void record(const LatencySample& sample);
    LatencySummary summary() const;
    int write_csv(const std::string& path) const;
    std::vector<LatencySample> samples() const;

    static uint64_t now_ns();
    static LatencySamplingMode parse_mode(const std::string& value);
    static std::string mode_name(LatencySamplingMode mode);

private:
    LatencySamplingMode mode_;
    size_t sample_size_;
    mutable std::mutex mutex_;
    std::vector<LatencySample> samples_;
    std::atomic<uint64_t> seen_{0};
    std::atomic<uint64_t> next_tx_id_{1};
};

#endif // LATENCY_SAMPLER_H
