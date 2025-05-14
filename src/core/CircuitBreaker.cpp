#include "CircuitBreaker.hpp"

#include <chrono>
#include <string>

bool CircuitBreaker::isTripped(const std::string& backendUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto tripped_it = tripped_backends_.find(backendUrl);

    if (tripped_it != tripped_backends_.end() && tripped_it->second > std::chrono::steady_clock::now()) {
        // Backend is currently in cooldown period
        logger_->error("Circuit breaker tripped for backend: " + backendUrl);
        // statsd_client_->increment(MetricsDefinitions::CIRCUIT_BREAKER_TRIPPED);
        return true;
    }
    // Either not tripped or cooldown expired
    return false;
}

void CircuitBreaker::trip(const std::string& backendUrl, std::chrono::milliseconds coolDownDuration) {
    std::lock_guard<std::mutex> lock(mutex_);
    tripped_backends_[backendUrl] = std::chrono::steady_clock::now() + coolDownDuration;
    logger_->error("Tripping circuit breaker for backend: " + backendUrl + " for " + std::to_string(coolDownDuration.count()) + "ms");
}