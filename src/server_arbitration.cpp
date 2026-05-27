#include "server_arbitration.h"
#include <algorithm>
#include <cmath>

ServerArbitrator::ServerArbitrator(const ServerArbitrationConfig& config, DSMObjectStore* store)
    : config_(config), store_(store), running_(false),
      queue_wait_p50_(0), queue_wait_p95_(0), queue_wait_p99_(0),
      arbitrated_commits_(0), arbitrated_aborts_(0) {
}

ServerArbitrator::~ServerArbitrator() {
    stop();
}

uint64_t ServerArbitrator::get_time_us() {
    auto now = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return us.count();
}

int ServerArbitrator::start() {
    if (running_) return -1;
    running_ = true;

    for (uint32_t i = 0; i < config_.worker_threads; i++) {
        workers_.emplace_back(&ServerArbitrator::worker_thread, this);
    }

    return 0;
}

void ServerArbitrator::stop() {
    running_ = false;
    queue_cv_.notify_all();

    for (auto& thread : workers_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    workers_.clear();
}

int ServerArbitrator::submit_request(const ArbitrationRequest& req) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (request_queue_.size() >= config_.queue_size) {
        return -1;  // Queue full
    }

    ArbitrationRequest req_copy = req;
    req_copy.submit_time_us = get_time_us();
    req_copy.status = ArbitrationRequest::PENDING;

    request_queue_.push(req_copy);
    queue_cv_.notify_one();

    return 0;
}

int ServerArbitrator::poll_completion(uint64_t tx_id, bool& committed) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = completed_requests_.find(tx_id);
    if (it == completed_requests_.end()) {
        return -1;  // Not found or still pending
    }

    committed = it->second.committed;
    completed_requests_.erase(it);
    return 0;
}

void ServerArbitrator::worker_thread() {
    while (running_) {
        ArbitrationRequest req;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { return !request_queue_.empty() || !running_; });

            if (!running_) break;
            if (request_queue_.empty()) continue;

            req = request_queue_.front();
            request_queue_.pop();
        }

        process_request(req);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            completed_requests_[req.tx_id] = req;

            // Track queue wait time
            uint64_t wait_us = req.complete_time_us - req.submit_time_us;
            stats_mutex_.lock();
            queue_wait_us_.push_back(wait_us);
            stats_mutex_.unlock();
        }

        queue_cv_.notify_all();
    }
}

void ServerArbitrator::process_request(ArbitrationRequest& req) {
    // Server executes the transaction sequentially for hot objects
    // 1. Read current values
    // 2. Apply writes
    // 3. Mark as committed

    req.committed = true;

    // Apply all writes
    for (const auto& p : req.write_values) {
        uint64_t object_id = p.first;
        uint64_t new_value = p.second;

        ObjectHeader* obj = store_->get_object_header(object_id);
        if (!obj) {
            req.committed = false;
            arbitrated_aborts_++;
            break;
        }

        // Apply write
        obj->value = new_value;
        obj->version++;
        obj->last_writer_tx_id = req.tx_id;
    }

    if (req.committed) {
        arbitrated_commits_++;
    } else {
        arbitrated_aborts_++;
    }

    req.complete_time_us = get_time_us();
    req.status = req.committed ? ArbitrationRequest::COMMITTED : ArbitrationRequest::ABORTED;
}

void ServerArbitrator::update_queue_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    if (queue_wait_us_.empty()) return;

    std::sort(queue_wait_us_.begin(), queue_wait_us_.end());

    size_t p50_idx = queue_wait_us_.size() * 50 / 100;
    size_t p95_idx = queue_wait_us_.size() * 95 / 100;
    size_t p99_idx = queue_wait_us_.size() * 99 / 100;

    queue_wait_p50_ = queue_wait_us_[p50_idx];
    queue_wait_p95_ = queue_wait_us_[p95_idx];
    queue_wait_p99_ = queue_wait_us_[p99_idx];
}
