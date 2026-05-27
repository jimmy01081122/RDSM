#ifndef HOT_DETECTION_H
#define HOT_DETECTION_H

#include "dsm_object.h"
#include <stdint.h>
#include <vector>
#include <map>
#include <mutex>
#include <deque>
#include <chrono>

struct HotDetectionConfig {
    bool enabled;
    double abort_threshold;      // e.g., 0.1 = 10% abort rate
    uint32_t window_ms;          // Sliding window in milliseconds
    uint32_t min_access_count;   // Minimum accesses to consider
};

struct HotObjectInfo {
    uint64_t object_id;
    uint64_t access_count;
    uint64_t abort_count;
    double abort_rate;
    uint64_t last_hot_timestamp_us;
    bool is_hot;
};

class HotDetector {
public:
    HotDetector(const HotDetectionConfig& config, DSMObjectStore* store);
    ~HotDetector();

    // Update stats for an object access
    void record_access(uint64_t object_id);
    void record_abort(uint64_t object_id);

    // Check if object is hot
    bool is_hot(uint64_t object_id);

    // Get all hot objects
    std::vector<HotObjectInfo> get_hot_objects();

    // Detect hot objects based on current stats
    void detect_hot_objects();

    uint64_t get_hot_object_count() const { return hot_objects_.size(); }

private:
    HotDetectionConfig config_;
    DSMObjectStore* store_;
    std::map<uint64_t, HotObjectInfo> hot_objects_;
    std::mutex hot_objects_mutex_;

    uint64_t get_time_us();
    bool should_mark_hot(uint64_t object_id);
};

#endif // HOT_DETECTION_H
