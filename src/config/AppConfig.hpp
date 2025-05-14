#ifndef APPCONFIG_HPP
#define APPCONFIG_HPP

#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <sstream>

#include "../models/BackendUrlInfo.hpp"

namespace LogUtils {
    enum LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        CERROR = 3,
        SETUP = 4
    };
    
    static std::string DEBUG_LOG_PREFIX = "[Debug] ";
    static std::string INFO_LOG_PREFIX = "[Info] ";
    static std::string WARN_LOG_PREFIX = "[Warning] ";
    static std::string CERROR_LOG_PREFIX = "[Error] ";
    static std::string SETUP_LOG_PREFIX = "[Setup] ";
}

// Note that only <metric.X> where 1 <= X <= 5 are valid.
namespace MetricsDefinitions {
    static std::string CODE_EXCEPTION = "metric.1";

    static std::string JSON_ERROR = "metric.2";

    static std::string CIRCUIT_BREAKER_LOGGED = "metric.3";

    // static std::string REQUEST_MADE_TO_BACKEND = "metric.4";

    static std::string REQUEST_TIMED_OUT = "metric.5";
    
    // static std::string BACKEND_RESPONSE_404 = "metric.6";
    
    // static std::string BACKEND_NO_RESPONSE_ERROR = "metric.6";
    
    // static std::string CIRCUIT_BREAKER_TRIPPED = "metric.6";

    // static std::string BACKEND_RESPONSE_STATUS_CODE_ERROR = "metric.6";
}

namespace Constants {
    static constexpr auto TIME_FORMAT = "%Y-%m-%dT%H:%M:%S";
    static const std::regex url_regex(R"(^(https?):\/\/([^:\/?#]+)(?::(\d+))?(?:[\/?#]|$))");
};

// --- Configuration Struct ---
class AppConfig {
public:    
    std::map<std::string, BackendUrlInfo> country_backend_map; // Key: Uppercase Country ISO
    
    // Cache configuration
    bool use_redis;
    std::string redis_host;
    int redis_port;
    int redis_ttl;
    int in_memory_cache_ttl;
    int in_memory_cache_max_size;

    // Server Congifuration
    int frontend_port;
    int number_of_threads_per_core;

    // Logging Level
    LogUtils::LogLevel log_level;

    // Metrics
    int metrics_batch_size;
    int metrics_send_interval_in_millis;

    // --- Request Handling ---
    // SLA
    int server_sla_in_micros;
    int request_average_processing_time_in_micros;
    bool drop_sla_timeout_requests;

    // Circuit Breaker Cool Off Period
    int backend_servers_circuit_breaker_cool_off_duration_in_millis;

    // Backend Servers Network Configurations
    int connection_timeout_in_microseconds;
    int read_request_timeout_in_microseconds;
    

    AppConfig() {
        // --- Set Defaults  ---
        server_sla_in_micros = 1000000;
        request_average_processing_time_in_micros = 1200;
        connection_timeout_in_microseconds = 25000;
        read_request_timeout_in_microseconds = 50000;
        backend_servers_circuit_breaker_cool_off_duration_in_millis = 10;
        number_of_threads_per_core = 2;
        drop_sla_timeout_requests = false; // Default to current behavior (send 504)

        // Configurable from Config
        use_redis = true;
        frontend_port = 9000;
        redis_host = "localhost";
        redis_port = 6379;
        redis_ttl = 3600 * 24; // 1 day
        in_memory_cache_ttl = redis_ttl;
        in_memory_cache_max_size = 10000;
        log_level = LogUtils::LogLevel::CERROR; // Default log level
        metrics_batch_size = 100;
        metrics_send_interval_in_millis = 1000;
    }

    std::string to_string() const {  // Made const for better usage
        std::stringstream ss;
        ss << "// --- Configuration Params Start --- //" << std::endl
            << "frontend_port: " << frontend_port << std::endl
            << "number_of_threads_per_core: " << number_of_threads_per_core << std::endl
            << "server_sla_in_micros: " << server_sla_in_micros << std::endl
            << "request_average_processing_time_in_micros: " << request_average_processing_time_in_micros << std::endl
            << "drop_sla_timeout_requests: " << std::boolalpha << drop_sla_timeout_requests << std::noboolalpha << std::endl
            << "// --- Cache Configuration --- //" << std::endl
            << "use_redis: " << std::boolalpha << use_redis << std::noboolalpha << std::endl
            << "redis_host: " << redis_host << std::endl
            << "redis_port: " << redis_port << std::endl
            << "redis_ttl: " << redis_ttl << std::endl
            << "in_memory_cache_ttl: " << in_memory_cache_ttl << std::endl
            << "in_memory_cache_max_size: " << in_memory_cache_max_size << std::endl
            << "// --- Logging & Metrics --- //" << std::endl
            << "log_level: " << static_cast<int>(log_level) << std::endl  // Cast enum to int
            << "metrics_batch_size: " << metrics_batch_size << std::endl
            << "metrics_send_interval_in_millis: " << metrics_send_interval_in_millis << std::endl
            << "--- Backend Servers Network Configurations --- " << std::endl
            << "circuit_breaker_cool_off_duration_in_millis: " << backend_servers_circuit_breaker_cool_off_duration_in_millis << std::endl
            << "connection_timeout_in_microseconds: " << connection_timeout_in_microseconds << std::endl
            << "read_request_timeout_in_microseconds: " << read_request_timeout_in_microseconds << std::endl;
        

        ss << "--- Country_ISO : BackendServer endpoint URL map ---" << std::endl;
        for (const auto& [country, backend] : country_backend_map) {
            ss << country << " : " << backend.url << std::endl;
        }
        ss << "// --- Configuration Params End --- //" << std::endl;
        return ss.str();
    }
};

#endif // APPCONFIG_HPP