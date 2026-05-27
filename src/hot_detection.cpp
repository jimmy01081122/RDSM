#include "hot_detection.h"
#include <algorithm>
#include <cmath>

HotDetector::HotDetector(const HotDetectionConfig& config, DSMObjectStore* store)
    : config_(config), store_(store) {
}

HotDetector::~HotDetector() {
}

uint64_t HotDetector::get_time_us() {
    auto now = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return us.count();
}

void HotDetector::record_access(uint64_t object_id) {
    if (!config_.enabled) return;

    std::lock_guard<std::mutex> lock(hot_objects_mutex_);
    if (hot_objects_.find(object_id) == hot_objects_.end()) {
        hot_objects_[object_id] = {object_id, 0, 0, 0.0, 0, false};
    }
    hot_objects_[object_id].access_count++;
}

void HotDetector::record_abort(uint64_t object_id) {
    if (!config_.enabled) return;

    std::lock_guard<std::mutex> lock(hot_objects_mutex_);
    if (hot_objects_.find(object_id) == hot_objects_.end()) {
        hot_objects_[object_id] = {object_id, 0, 0, 0.0, 0, false};
    }
    hot_objects_[object_id].abort_count++;
}

bool HotDetector::should_mark_hot(uint64_t object_id) {
    auto it = hot_objects_.find(object_id);
    if (it == hot_objects_.end()) return false;

    const HotObjectInfo& info = it->second;
    if (info.access_count < config_.min_access_count) {
        return false;
    }

    double abort_rate = (double)info.abort_count / info.access_count;
    return abort_rate > config_.abort_threshold;
}

void HotDetector::detect_hot_objects() {
    if (!config_.enabled) return;

    std::lock_guard<std::mutex> lock(hot_objects_mutex_);
    uint64_t now = get_time_us();

    for (auto& p : hot_objects_) {
        uint64_t object_id = p.first;
        HotObjectInfo& info = p.second;

        // Calculate abort rate
        if (info.access_count > 0) {
            info.abort_rate = (double)info.abort_count / info.access_count;
        }

        // Mark as hot if threshold exceeded
        bool was_hot = info.is_hot;
        info.is_hot = should_mark_hot(object_id);

        if (info.is_hot && !was_hot) {
            info.last_hot_timestamp_us = now;
        }
    }
}

bool HotDetector::is_hot(uint64_t object_id) {
    std::lock_guard<std::mutex> lock(hot_objects_mutex_);
    auto it = hot_objects_.find(object_id);
    if (it != hot_objects_.end()) {
        return it->second.is_hot;
    }
    return false;
}

std::vector<HotObjectInfo> HotDetector::get_hot_objects() {
    std::vector<HotObjectInfo> result;
    std::lock_guard<std::mutex> lock(hot_objects_mutex_);

    for (const auto& p : hot_objects_) {
        if (p.second.is_hot) {
            result.push_back(p.second);
        }
    }

    return result;
}
