#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cache/InMemoryCache.hpp"
#include "cache/RedisCache.hpp"
#include "config/AppConfig.hpp"
#include "core/Backendify.hpp"
#include "logging/ConsoleLogger.hpp"
#include "metrics/DummyStatsDClient.hpp"
#include "metrics/StatsDClient.hpp"
#include "utils/Utils.hpp"
#include "third_party/httplib.h"

using json = nlohmann::json;
using namespace std;

// --- Helper Function to Initialize Cache ---
std::shared_ptr<CacheInterface> initializeCache(const AppConfig& config, std::shared_ptr<ILogger> logger) {
    // Attempt to create and connect Redis cache instance
    auto redis_cache = std::make_shared<RedisCache>(config, logger);

    // Check if Redis cache connected successfully, otherwise fall back to InMemoryCache
    if (config.use_redis && redis_cache->isConnected()) {
        logger->setup("Redis cache connected successfully.");
        return redis_cache; // Return the connected Redis cache
    } else {
        logger->setup("Creating InMemoryCache.");
        return std::make_shared<InMemoryCache>(config.in_memory_cache_ttl, config.in_memory_cache_max_size);
    }
}

// --- Helper Function to Initialize StatsD Client ---
std::shared_ptr<IStatsDClient> initializeStatsDClient(const AppConfig& config, std::shared_ptr<ILogger> logger) {
    string statsd_server_endpoint;
#ifdef _WIN32
    // Windows implementation
    logger->debug("Windows Implementation for STATSD_SERVER env variable read");
    char* statsd_server_value = nullptr;
    size_t len;
    errno_t err = _dupenv_s(&statsd_server_value, &len, "STATSD_SERVER");
    if (err == 0 && statsd_server_value != nullptr) {
        statsd_server_endpoint = std::string(statsd_server_value);
        free(statsd_server_value);
    }
    logger->setup("WINDOWS : STATSD_SERVER endpoint : " + statsd_server_endpoint);
#else
    // Linux/Unix implementation
    logger->debug("Linux/Unix Implementation for STATSD_SERVER env variable read");
    const char* statsd_server_value = std::getenv("STATSD_SERVER");
    if (statsd_server_value != nullptr) {
        statsd_server_endpoint = std::string(statsd_server_value);
    }
    logger->setup("Linux/Unix : STATSD_SERVER endpoint : " + statsd_server_endpoint);
#endif

    // Initialize StatsD client based on the environment variable
    try {
        if (!statsd_server_endpoint.empty()) {
            logger->debug("STATSD_SERVER endpoint found. Creating real StatsDClient instance.");
            return StatsDClient::getInstance(config, logger, statsd_server_endpoint);
        } 
    } catch (const std::exception& e) {
        stringstream ss;
        ss << "Unhandled exception: " << e.what();
        logger->error(ss.str());
    }

    logger->error("StatsDClient failed to get created. Creating DummyStatsDClient instance.");
    return DummyStatsDClient::getInstance();
}

// --- Main Function ---
int main(int argc, char** argv) {
    std::stringstream ss;
    try {
        // Process command-line arguments.
        vector<string> args_vec;
        for (int i = 1; i < argc; ++i) {
            args_vec.push_back(argv[i]);
        }

        optional<map<string, string>> parsedArgsOpt = Utils::parseArguments(args_vec);
        if (!parsedArgsOpt) {
            // Use a temporary logger instance for early errors before config is loaded
            ConsoleLogger::getInstance(LogUtils::LogLevel::CERROR)->error("Failed to parse command-line arguments for Frontend server. Exiting.");
            return 1;
        }

        map<string, string> startupArguments = parsedArgsOpt.value(); // Use .value() as we checked for nullopt

        // Load Configuration
        AppConfig config = Utils::loadConfiguration(startupArguments);

        // Initialize the main logger *after* loading the config
        std::shared_ptr<ILogger> logger = ConsoleLogger::getInstance(config.log_level);
        logger->setup("Configuration loaded.");
        logger->setup(config.to_string());

        // Initialize the StatsD client using the helper function
        std::shared_ptr<IStatsDClient> statsd_client = initializeStatsDClient(config, logger);
        logger->setup("IStatsDClient instance created");

        // Initialize the cache instance using the helper function
        std::shared_ptr<CacheInterface> cache_instance = initializeCache(config, logger);
        logger->setup("CacheInterface created");
        
        // Setup the HTTP server (Frontend)
        httplib::Server server;
        Backendify backendify(cache_instance, statsd_client, config, logger); // Use the selected cache instance
        backendify.setupServer(server);

        // Start the frontend server
        if (logger->getLogLevel() <= LogUtils::LogLevel::DEBUG) {
            ss.str("");
            ss.clear();
            ss << "Starting Frontend server on 0.0.0.0:" << config.frontend_port << "..." << endl 
            << "Country-Specific Backends Configured: " << config.country_backend_map.size() << endl
            << "Using Redis cache at " << config.redis_host << ":" << config.redis_port;
            logger->debug(ss.str());
        }

        if (server.listen("0.0.0.0", config.frontend_port)) {
            logger->setup("Backendify Server listening on port " + config.frontend_port);
        } else {
            logger->error("Failed to start Frontend server on port " + config.frontend_port);
            return 1;
        }

        // Should not be reached unless server stops gracefully
        logger->debug("Frontend Server stopped.");
        return 0;
    } catch (const std::exception& e) {
        ss.str("");
        ss.clear();
        ss << "Unhandled exception: " << e.what();
        ConsoleLogger::getInstance(LogUtils::LogLevel::CERROR)->error(ss.str());
        return 1;
    } catch (...) {
        ConsoleLogger::getInstance(LogUtils::LogLevel::CERROR)->error("Unknown error occurred. Exiting.");
        return 1;
    }
}