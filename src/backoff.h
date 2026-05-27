#ifndef BACKOFF_H
#define BACKOFF_H

#include <stdint.h>
#include <chrono>
#include <random>
#include <thread>

enum BackoffPolicyType {
    NO_BACKOFF = 0,
    FIXED_BACKOFF = 1,
    EXPONENTIAL_BACKOFF = 2,
    RANDOMIZED_EXPONENTIAL_BACKOFF = 3,
    CONTENTION_AWARE_BACKOFF = 4
};

struct BackoffConfig {
    BackoffPolicyType policy;
    uint32_t base_us;      // Base backoff in microseconds
    uint32_t max_us;       // Maximum backoff cap
    uint32_t max_retries;
};

class BackoffManager {
public:
    BackoffManager(const BackoffConfig& config);
    ~BackoffManager();

    // Apply backoff based on abort reason and retry count
    void backoff(int abort_reason, uint32_t retry_count);

    // Reset state for new transaction
    void reset();

private:
    BackoffConfig config_;
    static thread_local std::mt19937 gen;

    uint32_t calculate_backoff_us(int abort_reason, uint32_t retry_count);
};

#endif // BACKOFF_H
