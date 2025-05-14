#ifndef CIRCUITBREAKER_HPP
#define CIRCUITBREAKER_HPP

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../interfaces/ILogger.hpp"
#include "../interfaces/IStatsDClient.hpp"
#include "../config/AppConfig.hpp" // For MetricsDefinitions

class CircuitBreaker {
public:
    CircuitBreaker(std::shared_ptr<ILogger> logger, std::shared_ptr<IStatsDClient> statsd_client)
        : logger_(logger), statsd_client_(statsd_client) {
        if (!logger_) {
            throw std::invalid_argument("Logger cannot be null for CircuitBreaker");
        }
        if (!statsd_client_) {
            throw std::invalid_argument("StatsDClient cannot be null for CircuitBreaker");
        }
    }

    // Checks if the circuit is tripped (in cooldown) for a given backend URL.
    // Returns true if tripped, false otherwise.
    bool isTripped(const std::string& backendUrl);

    // Trips the circuit breaker for a given backend URL for a specified duration.
    void trip(const std::string& backendUrl, std::chrono::milliseconds coolDownDuration);

private:
    std::shared_ptr<ILogger> logger_;
    std::shared_ptr<IStatsDClient> statsd_client_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> tripped_backends_;
};

#endif // CIRCUITBREAKER_HPP