#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "../config/AppConfig.hpp"
#include "../interfaces/ILogger.hpp"
#include "../interfaces/IStatsDClient.hpp"
#include "../third_party/UDPSender.hpp"

class StatsDClient : public IStatsDClient {
public:
    static std::shared_ptr<StatsDClient> getInstance(
        const AppConfig& config, 
        std::shared_ptr<ILogger> logger, 
        const std::string& stats_server_endpoint);
    ~StatsDClient(); // Move destructor to public section

    void increment(const std::string& key, int value = 1) override;
    void decrement(const std::string& key, int value = 1) override;
    void gauge(const std::string& key, double value) override;
    void timing(const std::string& key, std::chrono::milliseconds value) override;
    void set(const std::string& key, const std::string& value) override;

private:
    StatsDClient(
        const AppConfig& config, 
        std::shared_ptr<ILogger> logger, 
        const std::string& stats_server_endpoint);
    void send(const std::string& message);

    std::shared_ptr<ILogger> logger_;
    std::unique_ptr<Statsd::UDPSender> udp_sender_;

    static std::shared_ptr<StatsDClient> instance;
    static std::once_flag init_flag;

    // Delete copy and move operations
    StatsDClient(const StatsDClient&) = delete;
    StatsDClient& operator=(const StatsDClient&) = delete;
    StatsDClient(StatsDClient&&) = delete;
    StatsDClient& operator=(StatsDClient&&) = delete;
};