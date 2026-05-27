#ifndef SERVER_ARBITRATION_H
#define SERVER_ARBITRATION_H

#include "dsm_object.h"
#include <stdint.h>
#include <queue>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>

struct ArbitrationRequest {
    uint64_t tx_id;
    std::vector<uint64_t> write_object_ids;
    std::map<uint64_t, uint64_t> write_values;
    uint64_t submit_time_us;
    uint64_t complete_time_us;
    bool committed;
    enum { PENDING, COMMITTED, ABORTED } status;
};

struct ServerArbitrationConfig {
    bool enabled;
    std::string mode;              // "fifo"
    uint32_t queue_size;
    uint32_t worker_threads;
};

class ServerArbitrator {
public:
    ServerArbitrator(const ServerArbitrationConfig& config, DSMObjectStore* store);
    ~ServerArbitrator();

    // Start worker threads
    int start();

    // Stop worker threads
    void stop();

    // Submit request to arbitration queue
    int submit_request(const ArbitrationRequest& req);

    // Poll for completion (non-blocking)
    int poll_completion(uint64_t tx_id, bool& committed);

    // Get server queue statistics
    double get_queue_wait_p50_us() const { return queue_wait_p50_; }
    double get_queue_wait_p95_us() const { return queue_wait_p95_; }
    double get_queue_wait_p99_us() const { return queue_wait_p99_; }

    uint64_t get_arbitrated_commits() const { return arbitrated_commits_; }
    uint64_t get_arbitrated_aborts() const { return arbitrated_aborts_; }

private:
    ServerArbitrationConfig config_;
    DSMObjectStore* store_;

    // FIFO queue
    std::queue<ArbitrationRequest> request_queue_;
    std::map<uint64_t, ArbitrationRequest> completed_requests_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Worker threads
    std::vector<std::thread> workers_;
    bool running_;

    // Statistics
    std::vector<uint64_t> queue_wait_us_;
    double queue_wait_p50_, queue_wait_p95_, queue_wait_p99_;
    uint64_t arbitrated_commits_, arbitrated_aborts_;
    std::mutex stats_mutex_;

    // Worker thread function
    void worker_thread();

    // Process a single request
    void process_request(ArbitrationRequest& req);

    // Update statistics
    void update_queue_stats();

    uint64_t get_time_us();
};

#endif // SERVER_ARBITRATION_H
