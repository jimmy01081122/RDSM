#include "backoff.h"
#include <algorithm>

thread_local std::mt19937 BackoffManager::gen(std::random_device{}());

BackoffManager::BackoffManager(const BackoffConfig& config)
    : config_(config) {
}

BackoffManager::~BackoffManager() {
}

uint32_t BackoffManager::calculate_backoff_us(int abort_reason, uint32_t retry_count) {
    uint32_t backoff_us = 0;

    switch (config_.policy) {
    case NO_BACKOFF:
        backoff_us = 0;
        break;

    case FIXED_BACKOFF:
        backoff_us = config_.base_us;
        break;

    case EXPONENTIAL_BACKOFF:
        backoff_us = config_.base_us * (1 << std::min(retry_count, 10U));
        backoff_us = std::min(backoff_us, config_.max_us);
        break;

    case RANDOMIZED_EXPONENTIAL_BACKOFF: {
        uint32_t exp_backoff = config_.base_us * (1 << std::min(retry_count, 10U));
        exp_backoff = std::min(exp_backoff, config_.max_us);
        std::uniform_int_distribution<> dis(config_.base_us, exp_backoff);
        backoff_us = dis(gen);
        break;
    }

    case CONTENTION_AWARE_BACKOFF: {
        // LOCK_FAIL = 0, VALIDATION_FAIL = 1
        if (abort_reason == 0) {  // LOCK_FAIL - short delay
            std::uniform_int_distribution<> dis(10, 50);
            backoff_us = dis(gen);
        } else if (abort_reason == 1) {  // VALIDATION_FAIL - medium delay
            std::uniform_int_distribution<> dis(50, 200);
            backoff_us = dis(gen);
        } else {
            // Exponential for repeated failures
            uint32_t exp_backoff = config_.base_us * (1 << std::min(retry_count, 8U));
            exp_backoff = std::min(exp_backoff, config_.max_us);
            std::uniform_int_distribution<> dis(config_.base_us, exp_backoff);
            backoff_us = dis(gen);
        }
        break;
    }

    default:
        backoff_us = 0;
    }

    return backoff_us;
}

void BackoffManager::backoff(int abort_reason, uint32_t retry_count) {
    uint32_t backoff_us = calculate_backoff_us(abort_reason, retry_count);

    if (backoff_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
    }
}

void BackoffManager::reset() {
    // Nothing to reset for basic implementation
}
