#include "dsm_object.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdexcept>

DSMObjectStore::DSMObjectStore()
    : max_objects_(MAX_OBJECTS), object_count_(0) {
    // Allocate RDMA-visible memory for objects
    objects_ = (ObjectHeader*)malloc(MAX_OBJECTS * sizeof(ObjectHeader));
    if (!objects_) {
        throw std::runtime_error("Failed to allocate DSM object store");
    }
    memset(objects_, 0, MAX_OBJECTS * sizeof(ObjectHeader));

    // Initialize latency histogram (5 buckets)
    for (auto& bucket : global_stats_.latency_histogram) {
        bucket.store(0);
    }
    object_mutexes_.reserve(MAX_OBJECTS);
    object_arbitration_mutexes_.reserve(MAX_OBJECTS);
    for (uint32_t i = 0; i < MAX_OBJECTS; ++i) {
        object_mutexes_.push_back(std::make_unique<std::mutex>());
        object_arbitration_mutexes_.push_back(std::make_unique<std::mutex>());
    }
    shard_arbitration_mutexes_.reserve(MAX_ARBITRATION_SHARDS);
    for (uint32_t i = 0; i < MAX_ARBITRATION_SHARDS; ++i) {
        shard_arbitration_mutexes_.push_back(std::make_unique<std::mutex>());
    }
    object_queue_depths_ = std::vector<std::atomic<uint64_t>>(MAX_OBJECTS);
    shard_queue_depths_ = std::vector<std::atomic<uint64_t>>(MAX_ARBITRATION_SHARDS);
}

DSMObjectStore::~DSMObjectStore() {
    if (objects_) {
        free(objects_);
        objects_ = nullptr;
    }
}

int DSMObjectStore::create_object(uint64_t object_id, ObjectType type, uint64_t init_value) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    if (object_count_ >= MAX_OBJECTS) {
        return -1;
    }

    ObjectHeader* obj = &objects_[object_count_];
    obj->object_id = object_id;
    obj->version = 0;
    obj->lock_owner = 0;
    obj->lock_bit = 0;
    obj->object_type = static_cast<uint32_t>(type);
    obj->last_writer_tx_id = 0;
    obj->value = init_value;

    object_stats_[object_id].is_hot.store(false);
    object_count_++;

    return 0;
}

ObjectHeader* DSMObjectStore::get_object_header(uint64_t object_id) {
    // Linear search (could optimize with map)
    for (uint64_t i = 0; i < object_count_; i++) {
        if (objects_[i].object_id == object_id) {
            return &objects_[i];
        }
    }
    return nullptr;
}

DSMObjectStore::ObjectStats* DSMObjectStore::get_object_stats(uint64_t object_id) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto it = object_stats_.find(object_id);
    if (it != object_stats_.end()) {
        return &it->second;
    }
    return nullptr;
}

int DSMObjectStore::verify_invariants(uint64_t initial_stock, uint64_t& final_stock, uint64_t& sold_count) {
    // Invariants for flash-sale inventory:
    // stock >= 0
    // user_balance >= 0
    // sold_count + stock = initial_stock
    // no duplicate order commits

    bool found_sold_object = false;
    final_stock = 0;
    sold_count = 0;

    std::lock_guard<std::mutex> data_lock(mutation_mutex_);

    // Sum all stock objects and find sold_count.
    for (uint64_t i = 0; i < object_count_; i++) {
        if (objects_[i].object_type == TYPE_PRODUCT_STOCK) {
            final_stock += objects_[i].value;
        } else if (objects_[i].object_type == TYPE_SOLD_COUNT) {
            found_sold_object = true;
            sold_count += objects_[i].value;
        }
    }

    if (!found_sold_object) {
        global_stats_.invariant_violation_count++;
        return -1;
    }

    if (sold_count + final_stock != initial_stock) {
        global_stats_.invariant_violation_count++;
        return -1;
    }

    // Check all user balances >= 0
    for (uint64_t i = 0; i < object_count_; i++) {
        if (objects_[i].object_type == TYPE_USER_BALANCE && (int64_t)objects_[i].value < 0) {
            global_stats_.invariant_violation_count++;
            return -1;
        }
    }

    return 0;
}

std::mutex& DSMObjectStore::object_mutex(uint64_t object_id) {
    if (object_id >= object_mutexes_.size()) {
        throw std::out_of_range("object mutex id out of range");
    }
    return *object_mutexes_[object_id];
}

std::mutex& DSMObjectStore::object_arbitration_mutex(uint64_t object_id) {
    if (object_id >= object_arbitration_mutexes_.size()) {
        throw std::out_of_range("object arbitration id out of range");
    }
    return *object_arbitration_mutexes_[object_id];
}

std::mutex& DSMObjectStore::shard_arbitration_mutex(uint64_t shard_id) {
    shard_id %= shard_arbitration_mutexes_.size();
    return *shard_arbitration_mutexes_[shard_id];
}

std::atomic<uint64_t>& DSMObjectStore::object_queue_depth(uint64_t object_id) {
    if (object_id >= object_queue_depths_.size()) {
        throw std::out_of_range("object queue id out of range");
    }
    return object_queue_depths_[object_id];
}

std::atomic<uint64_t>& DSMObjectStore::shard_queue_depth(uint64_t shard_id) {
    shard_id %= shard_queue_depths_.size();
    return shard_queue_depths_[shard_id];
}

void DSMObjectStore::record_queue_wait_sample(uint64_t value_us) {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    if (queue_wait_samples_.size() < MAX_SAMPLES) {
        queue_wait_samples_.push_back(value_us);
    }
}

void DSMObjectStore::record_queue_length_sample(uint64_t value) {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    if (queue_length_samples_.size() < MAX_SAMPLES) {
        queue_length_samples_.push_back(value);
    }
}

void DSMObjectStore::record_service_time_sample(uint64_t value_us) {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    if (service_time_samples_.size() < MAX_SAMPLES) {
        service_time_samples_.push_back(value_us);
    }
}

std::vector<uint64_t> DSMObjectStore::queue_wait_samples() {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    return queue_wait_samples_;
}

std::vector<uint64_t> DSMObjectStore::queue_length_samples() {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    return queue_length_samples_;
}

std::vector<uint64_t> DSMObjectStore::service_time_samples() {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    return service_time_samples_;
}
