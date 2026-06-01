#include "latency_sampler.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string_view>

namespace {

double percentile(std::vector<uint64_t> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double idx = (p / 100.0) * static_cast<double>(values.size() - 1);
    return static_cast<double>(values[static_cast<size_t>(idx + 0.5)]);
}

std::string csv_escape(std::string_view value) {
    std::string escaped = "\"";
    for (char c : value) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += "\"";
    return escaped;
}

bool is_abort_status(std::string_view status) {
    return status != "committed";
}

uint64_t tx_latency_us(const LatencySample& sample) {
    if (sample.tx_end_ns <= sample.tx_start_ns) {
        return 0;
    }
    return (sample.tx_end_ns - sample.tx_start_ns) / 1000;
}

} // namespace

LatencySampler::LatencySampler(LatencySamplingMode mode, size_t sample_size)
    : mode_(mode), sample_size_(sample_size) {
    if (mode_ != LatencySamplingMode::Off && sample_size_ == 0) {
        sample_size_ = 1;
    }
    if (mode_ == LatencySamplingMode::Full) {
        sample_size_ = 0;
    }
}

uint64_t LatencySampler::next_tx_id() {
    return next_tx_id_.fetch_add(1);
}

bool LatencySampler::enabled() const {
    return mode_ != LatencySamplingMode::Off;
}

void LatencySampler::record(const LatencySample& sample) {
    if (!enabled()) {
        return;
    }
    const uint64_t index = seen_.fetch_add(1);
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == LatencySamplingMode::Full) {
        samples_.push_back(sample);
        return;
    }
    if (samples_.size() < sample_size_) {
        samples_.push_back(sample);
        return;
    }

    // Deterministic bounded rotation buffer (NOT true reservoir sampling):
    // After the buffer is full, new samples overwrite old ones in round-robin
    // order (index % sample_size_). This introduces recency bias: later
    // samples are systematically over-represented compared to earlier ones.
    // True reservoir sampling (Algorithm R) would use random replacement with
    // probability (sample_size_ / index) to ensure uniform representation.
    samples_[index % sample_size_] = sample;
}

LatencySummary LatencySampler::summary() const {
    LatencySummary result{};
    std::vector<LatencySample> copy = samples();
    result.latency_sample_count = copy.size();

    std::vector<uint64_t> all_latency;
    std::vector<uint64_t> committed_latency;
    std::vector<uint64_t> abort_latency;
    std::vector<uint64_t> cold_occ_latency;
    std::vector<uint64_t> hot_arbitration_latency;
    std::vector<uint64_t> retry_counts;

    for (const auto& sample : copy) {
        uint64_t latency_us = tx_latency_us(sample);
        all_latency.push_back(latency_us);
        retry_counts.push_back(sample.retry_count);
        if (sample.final_status == "committed") {
            committed_latency.push_back(latency_us);
        } else if (is_abort_status(sample.final_status)) {
            abort_latency.push_back(latency_us);
        }
        if (sample.path_type == "cold_occ" || sample.path_type == "adaptive_occ") {
            cold_occ_latency.push_back(latency_us);
        } else if (sample.path_type == "hot_arbitration" ||
                   sample.path_type == "adaptive_arbitration") {
            hot_arbitration_latency.push_back(latency_us);
        }
    }

    result.tx_latency_us_p50 = percentile(all_latency, 50);
    result.tx_latency_us_p95 = percentile(all_latency, 95);
    result.tx_latency_us_p99 = percentile(all_latency, 99);
    result.tx_latency_us_max = all_latency.empty() ? 0.0 : static_cast<double>(*std::max_element(all_latency.begin(), all_latency.end()));
    result.committed_tx_latency_us_p50 = percentile(committed_latency, 50);
    result.committed_tx_latency_us_p95 = percentile(committed_latency, 95);
    result.committed_tx_latency_us_p99 = percentile(committed_latency, 99);
    result.abort_tx_latency_us_p50 = percentile(abort_latency, 50);
    result.abort_tx_latency_us_p95 = percentile(abort_latency, 95);
    result.abort_tx_latency_us_p99 = percentile(abort_latency, 99);
    result.cold_occ_latency_us_p50 = percentile(cold_occ_latency, 50);
    result.cold_occ_latency_us_p95 = percentile(cold_occ_latency, 95);
    result.cold_occ_latency_us_p99 = percentile(cold_occ_latency, 99);
    result.hot_arbitration_latency_us_p50 = percentile(hot_arbitration_latency, 50);
    result.hot_arbitration_latency_us_p95 = percentile(hot_arbitration_latency, 95);
    result.hot_arbitration_latency_us_p99 = percentile(hot_arbitration_latency, 99);
    result.retry_count_p50 = percentile(retry_counts, 50);
    result.retry_count_p95 = percentile(retry_counts, 95);
    result.retry_count_p99 = percentile(retry_counts, 99);
    return result;
}

int LatencySampler::write_csv(const std::string& path) const {
    if (path.empty() || !enabled()) {
        return 0;
    }
    std::ofstream out(path);
    if (!out) {
        return -1;
    }
    out << "tx_id,thread_id,workload_name,application_case,algorithm,arbitration_mode,hot_shards,"
        << "sold_counter_mode,path_type,appendix_only,appendix_reason,tx_start_ns,read_phase_done_ns,"
        << "route_decision_ns,queue_enter_ns,queue_leave_ns,commit_start_ns,commit_done_ns,tx_end_ns,"
        << "retry_count,lock_fail_count_for_tx,validation_fail_count_for_tx,final_status,"
        << "objects_touched,hot_objects_touched\n";

    for (const auto& sample : samples()) {
        out << sample.tx_id << ','
            << sample.thread_id << ','
            << csv_escape(sample.workload_name) << ','
            << csv_escape(sample.application_case) << ','
            << csv_escape(sample.algorithm) << ','
            << csv_escape(sample.arbitration_mode) << ','
            << sample.hot_shards << ','
            << csv_escape(sample.sold_counter_mode) << ','
            << csv_escape(sample.path_type) << ','
            << (sample.appendix_only ? "true" : "false") << ','
            << csv_escape(sample.appendix_reason) << ','
            << sample.tx_start_ns << ','
            << sample.read_phase_done_ns << ','
            << sample.route_decision_ns << ','
            << sample.queue_enter_ns << ','
            << sample.queue_leave_ns << ','
            << sample.commit_start_ns << ','
            << sample.commit_done_ns << ','
            << sample.tx_end_ns << ','
            << sample.retry_count << ','
            << sample.lock_fail_count_for_tx << ','
            << sample.validation_fail_count_for_tx << ','
            << csv_escape(sample.final_status) << ','
            << sample.objects_touched << ','
            << sample.hot_objects_touched << '\n';
    }
    return 0;
}

std::vector<LatencySample> LatencySampler::samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

uint64_t LatencySampler::now_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

LatencySamplingMode LatencySampler::parse_mode(const std::string& value) {
    if (value == "off") return LatencySamplingMode::Off;
    if (value == "full") return LatencySamplingMode::Full;
    if (value == "bounded_rotation" || value == "reservoir") {
        return LatencySamplingMode::BoundedRotation;
    }
    throw std::runtime_error("invalid --latency-sampling: " + value);
}

std::string LatencySampler::mode_name(LatencySamplingMode mode) {
    if (mode == LatencySamplingMode::Off) return "off";
    if (mode == LatencySamplingMode::Full) return "full";
    return "bounded_rotation";
}
