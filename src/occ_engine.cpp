#include "occ_engine.h"
#include <cstring>
#include <algorithm>
#include <atomic>

OCCEngine::OCCEngine(DSMObjectStore* store)
    : store_(store), next_tx_id_(1) {
}

OCCEngine::~OCCEngine() {
}

void OCCEngine::begin_transaction(Transaction& tx) {
    tx.tx_id = next_tx_id_++;
    tx.read_set_ids.clear();
    tx.read_set_versions.clear();
    tx.write_set.clear();
    tx.retry_count = 0;
    tx.abort_reason = ABORT_SYSTEM_ERROR;
    tx.start_time_us = get_time_us();
    tx.commit_time_us = 0;
    tx.path_type = COLD_PATH;
    tx.committed = false;
    store_->get_global_stats()->attempted_tx++;
}

int OCCEngine::read_object(Transaction& tx, uint64_t object_id, uint64_t& value, uint64_t& version) {
    ObjectHeader* obj = store_->get_object_header(object_id);
    if (!obj) {
        return -1;
    }

    // Wait until object is unlocked
    int retry = 0;
    while (obj->lock_bit && retry < 1000) {
        retry++;
        // Spin-wait (in real RDMA, would use RDMA READ polling)
    }

    if (obj->lock_bit) {
        return -1;  // Timeout
    }

    // Read value and version
    value = obj->value;
    version = obj->version;

    // Record in read set
    tx.read_set_ids.push_back(object_id);
    tx.read_set_versions.push_back(version);

    // Update stats
    auto stats = store_->get_object_stats(object_id);
    if (stats) {
        stats->access_count++;
    }

    return 0;
}

void OCCEngine::write_object(Transaction& tx, uint64_t object_id, uint64_t value) {
    // Just buffer the write locally
    tx.write_set[object_id] = value;
}

int OCCEngine::try_acquire_locks(Transaction& tx) {
    // Try to acquire locks on all write_set objects
    for (auto& p : tx.write_set) {
        uint64_t object_id = p.first;
        ObjectHeader* obj = store_->get_object_header(object_id);
        if (!obj) {
            store_->get_global_stats()->lock_fail_count++;
            tx.abort_reason = ABORT_LOCK_FAIL;
            return -1;
        }

        // Atomic CAS: if lock_bit == 0, set lock_bit = 1 and lock_owner = tx_id
        bool locked = false;
        for (int retry = 0; retry < 10; retry++) {
            if (!obj->lock_bit) {
                obj->lock_bit = 1;
                obj->lock_owner = tx.tx_id;
                locked = true;
                break;
            }
            // Spin a bit
        }

        if (!locked) {
            store_->get_global_stats()->lock_fail_count++;
            auto stats = store_->get_object_stats(object_id);
            if (stats) {
                stats->lock_fail_count++;
            }
            tx.abort_reason = ABORT_LOCK_FAIL;
            return -1;
        }
    }

    return 0;
}

int OCCEngine::validate_read_set(Transaction& tx) {
    // Check if any read_set versions have changed
    for (size_t i = 0; i < tx.read_set_ids.size(); i++) {
        uint64_t object_id = tx.read_set_ids[i];
        uint64_t expected_version = tx.read_set_versions[i];

        ObjectHeader* obj = store_->get_object_header(object_id);
        if (!obj || obj->version != expected_version) {
            store_->get_global_stats()->validation_fail_count++;
            auto stats = store_->get_object_stats(object_id);
            if (stats) {
                stats->validation_fail_count++;
                stats->abort_count++;
            }
            tx.abort_reason = ABORT_VALIDATION_FAIL;
            return -1;
        }
    }

    return 0;
}

int OCCEngine::apply_write_set(Transaction& tx) {
    // Apply all writes and update versions
    for (auto& p : tx.write_set) {
        uint64_t object_id = p.first;
        uint64_t new_value = p.second;

        ObjectHeader* obj = store_->get_object_header(object_id);
        if (!obj) {
            return -1;
        }

        obj->value = new_value;
        obj->version++;
        obj->last_writer_tx_id = tx.tx_id;

        auto stats = store_->get_object_stats(object_id);
        if (stats) {
            stats->write_count++;
        }
    }

    store_->get_global_stats()->committed_tx++;
    tx.committed = true;

    return 0;
}

void OCCEngine::release_locks(Transaction& tx) {
    // Release locks on all write_set objects
    for (auto& p : tx.write_set) {
        uint64_t object_id = p.first;
        ObjectHeader* obj = store_->get_object_header(object_id);
        if (obj && obj->lock_owner == tx.tx_id) {
            obj->lock_bit = 0;
            obj->lock_owner = 0;
        }
    }
}

int OCCEngine::commit_transaction(Transaction& tx) {
    // Baseline OCC commit phase:
    // 1. Acquire locks on all write_set objects
    // 2. Validate read_set versions
    // 3. Apply write_set
    // 4. Release locks

    if (try_acquire_locks(tx) != 0) {
        return -1;
    }

    if (validate_read_set(tx) != 0) {
        release_locks(tx);
        store_->get_global_stats()->aborted_tx++;
        return -1;
    }

    if (apply_write_set(tx) != 0) {
        release_locks(tx);
        store_->get_global_stats()->aborted_tx++;
        return -1;
    }

    release_locks(tx);

    tx.commit_time_us = get_time_us();
    uint64_t latency_us = tx.commit_time_us - tx.start_time_us;
    store_->get_global_stats()->total_commit_latency_us += latency_us;

    // Update latency histogram
    auto stats = store_->get_global_stats();
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

    return 0;
}

int OCCEngine::read_with_lock(uint64_t object_id, uint64_t& value, uint64_t& version) {
    ObjectHeader* obj = store_->get_object_header(object_id);
    if (!obj) {
        return -1;
    }

    // Read without waiting for lock (optimistic)
    value = obj->value;
    version = obj->version;
    return 0;
}
