#include "dsm_object.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

DSMObjectStore::DSMObjectStore()
    : max_objects_(MAX_OBJECTS), object_count_(0) {
    // Allocate RDMA-visible memory for objects
    objects_ = (ObjectHeader*)malloc(MAX_OBJECTS * sizeof(ObjectHeader));
    if (!objects_) {
        throw std::runtime_error("Failed to allocate DSM object store");
    }
    memset(objects_, 0, MAX_OBJECTS * sizeof(ObjectHeader));

    // Initialize latency histogram (5 buckets)
    global_stats_.latency_histogram.resize(5, 0);
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

    ObjectHeader* sold_obj = nullptr;
    final_stock = 0;

    std::lock_guard<std::mutex> data_lock(mutation_mutex_);

    // Sum all stock objects and find sold_count.
    for (uint64_t i = 0; i < object_count_; i++) {
        if (objects_[i].object_type == TYPE_PRODUCT_STOCK) {
            final_stock += objects_[i].value;
        } else if (objects_[i].object_type == TYPE_SOLD_COUNT) {
            sold_obj = &objects_[i];
        }
    }

    if (!sold_obj) {
        global_stats_.invariant_violation_count++;
        return -1;
    }

    sold_count = sold_obj->value;

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
