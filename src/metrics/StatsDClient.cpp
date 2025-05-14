#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "StatsDClient.hpp"

// // Define static members
std::shared_ptr<StatsDClient> StatsDClient::instance = nullptr;
std::once_flag StatsDClient::init_flag;

// Singleton instance
std::shared_ptr<StatsDClient> StatsDClient::getInstance(
    const AppConfig& config, 
    std::shared_ptr<ILogger> logger, 
    const std::string& stats_server_endpoint) {
    std::call_once(init_flag, [config, logger, stats_server_endpoint]() {
        instance = std::shared_ptr<StatsDClient>(new StatsDClient(config, logger, stats_server_endpoint));
    });

    return std::shared_ptr<StatsDClient>(instance.get());
}

// Constructor
StatsDClient::StatsDClient(
    const AppConfig& config, 
    std::shared_ptr<ILogger> logger, 
    const std::string& statsd_address) : logger_(logger), udp_sender_(nullptr) {
    auto colon_pos = statsd_address.find(':');
    if (colon_pos == std::string::npos) {
        throw std::runtime_error("STATSD_SERVER must be in the format <host>:<port>");
    }

    std::string host_ = statsd_address.substr(0, colon_pos);
    if (host_ == "localhost") {
        host_ = "127.0.0.1";
    }

    uint16_t port;
    try {
        port = static_cast<uint16_t>(std::stoi(statsd_address.substr(colon_pos + 1)));
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid port in STATSD_SERVER: " + std::string(e.what()));
    }

    // Create the UDPSender instance
    try {
        udp_sender_ = std::make_unique<Statsd::UDPSender>(host_, port, config.metrics_batch_size, config.metrics_send_interval_in_millis);
        logger_->setup("UDPSender initialized for " + host_ + ":" + std::to_string(port));
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to initialize UDPSender: " + std::string(e.what()));
    }
}


// Destructor
StatsDClient::~StatsDClient() {
    // No need to explicitly close socket, unique_ptr manages UDPSender lifetime
    logger_->debug("StatsDClient destroyed.");
}

// Send a message to the StatsD server
void StatsDClient::send(const std::string& message) {
    if (!udp_sender_) {
        logger_->error("StatsDClient: UDPSender is not initialized, cannot send message.");
        return;
    }
    try {
        udp_sender_->send(message);
    } catch (const std::exception& e) {
        // Log errors from UDPSender::send
        logger_->error("StatsDClient: Failed to send UDP message: " + std::string(e.what()));
    }
}

// Increment a counter
void StatsDClient::increment(const std::string& key, int value) {
    std::stringstream ss;
    ss << key << ":" << value << "|c";
    send(ss.str());
}

// Decrement a counter
void StatsDClient::decrement(const std::string& key, int value) {
    increment(key, -value);
}

// Record a gauge value
void StatsDClient::gauge(const std::string& key, double value) {
    std::stringstream ss;
    ss << key << ":" << value << "|g";
    send(ss.str());
}

// Record a timing value
void StatsDClient::timing(const std::string& key, std::chrono::milliseconds value) {
    std::stringstream ss;
    ss << key << ":" << value.count() << "|ms";
    send(ss.str());
}

// Record a set value
void StatsDClient::set(const std::string& key, const std::string& value) {
    std::stringstream ss;
    ss << key << ":" << value << "|s";
    send(ss.str());
}