#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/asio/signal_set.hpp> // For graceful shutdown
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp> // For make_work_guard
#include <boost/asio/steady_timer.hpp>

#include <nlohmann/json.hpp>

#include "cache/InMemoryCache.hpp"
#include "cache/RedisCache.hpp"
#include "config/AppConfig.hpp"
#include "core/Backendify.hpp"
#include "core/BeastHttpServer.hpp" // Include new Beast server
#include "logging/ConsoleLogger.hpp"
#include "metrics/DummyStatsDClient.hpp"
#include "metrics/StatsDClient.hpp"
#include "utils/Utils.hpp"
// #include "third_party/httplib.h" // REMOVE HTTPLIB

using json = nlohmann::json;
using namespace std;

// --- Helper Function to Initialize Cache ---
std::shared_ptr<CacheInterface> initializeCache(const AppConfig& config_, std::shared_ptr<ILogger> logger_) {
    // Attempt to create and connect Redis cache instance
    auto redis_cache = std::make_shared<RedisCache>(config_, logger_);

    // Check if Redis cache connected successfully, otherwise fall back to InMemoryCache
    if (config_.use_redis && redis_cache->isConnected()) {
        logger_->setup("Redis cache connected successfully.");
        return redis_cache; // Return the connected Redis cache
    } else {
        logger_->setup("Creating InMemoryCache.");
        return std::make_shared<InMemoryCache>(config_.in_memory_cache_ttl, config_.in_memory_cache_max_size);
    }
}

// --- Helper Function to Initialize StatsD Client ---
std::shared_ptr<IStatsDClient> initializeStatsDClient(const AppConfig& config, std::shared_ptr<ILogger> logger_) {
    string statsd_server_endpoint;
#ifdef _WIN32
    // Windows implementation
    logger_->debug("Windows Implementation for STATSD_SERVER env variable read");
    char* statsd_server_value = nullptr;
    size_t len;
    errno_t err = _dupenv_s(&statsd_server_value, &len, "STATSD_SERVER");
    if (err == 0 && statsd_server_value != nullptr) {
        statsd_server_endpoint = std::string(statsd_server_value);
        free(statsd_server_value);
    }
    logger_->setup("WINDOWS : STATSD_SERVER endpoint : " + statsd_server_endpoint);
#else
    // Linux/Unix implementation
    logger_->debug("Linux/Unix Implementation for STATSD_SERVER env variable read");
    const char* statsd_server_value = std::getenv("STATSD_SERVER");
    if (statsd_server_value != nullptr) {
        statsd_server_endpoint = std::string(statsd_server_value);
    }
    logger_->setup("Linux/Unix : STATSD_SERVER endpoint : " + statsd_server_endpoint);
#endif

    // Initialize StatsD client based on the environment variable
    try {
        if (!statsd_server_endpoint.empty()) {
            logger_->debug("STATSD_SERVER endpoint found. Creating real StatsDClient instance.");
            return StatsDClient::getInstance(config, logger_, statsd_server_endpoint);
        } 
    } catch (const std::exception& e) {
        stringstream ss;
        ss << "Unhandled exception: " << e.what();
        logger_->error(ss.str());
    }

    logger_->error("StatsDClient failed to get created. Creating DummyStatsDClient instance.");
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
        AppConfig config_ = Utils::loadConfiguration(startupArguments);

        // Initialize the main logger *after* loading the config
        std::shared_ptr<ILogger> logger_ = ConsoleLogger::getInstance(config_.log_level);
        logger_->setup("Configuration loaded.");
        logger_->setup(config_.to_string());

        // Initialize the StatsD client using the helper function
        std::shared_ptr<IStatsDClient> statsd_client = initializeStatsDClient(config_, logger_);
        logger_->setup("IStatsDClient instance created");

        // Initialize the cache instance using the helper function
        std::shared_ptr<CacheInterface> cache_instance = initializeCache(config_, logger_);
        logger_->setup("CacheInterface created");

        // --- Setup Boost.Asio io_context for both client and server operations ---
        boost::asio::io_context ioc;
        auto work_guard = boost::asio::make_work_guard(ioc); // Keep ioc.run() from returning if no work

        std::vector<std::thread> ioc_threads;
        logger_->setup("Starting " + std::to_string(config_.num_io_threads) + " I/O threads for Boost.Asio.");

        for (unsigned int i = 0; i < config_.num_io_threads; ++i) {
            ioc_threads.emplace_back([&ioc, logger_, i]() {
                // Moved std::cerr to be the very first thing in the thread lambda
                logger_->debug("Boost.Asio I/O thread " + std::to_string(i) + " started.");
                try {
                    ioc.run();
                    // This line should ideally only be reached after ioc.stop() or work_guard is reset
                    std::cerr << "[IO_THREAD_DEBUG] I/O Thread " << i << " ioc.run() returned." << std::endl;
                } catch (const std::exception& e) {
                    std::stringstream ss_err;
                    ss_err << "[IO_THREAD_DEBUG] Exception in Boost.Asio I/O thread " << i << ": " << e.what();
                    logger_->error(ss_err.str());
                } catch (...) {
                    std::stringstream ss_err;
                    ss_err << "[IO_THREAD_DEBUG] Unknown exception in Boost.Asio I/O thread " << i;
                    logger_->error(ss_err.str());
                }
                // Log when the thread lambda is actually exiting
                logger_->debug("[IO_THREAD_DEBUG] Boost.Asio I/O thread " + std::to_string(i) + " lambda exiting.");
            });
        }
        
        // Create and run the Beast HTTP server
        auto backendify_service = std::make_shared<Backendify>(ioc, cache_instance, statsd_client, config_, logger_);
        
        auto const address = net::ip::make_address("0.0.0.0");
        auto const port = static_cast<unsigned short>(config_.frontend_port);

        auto beast_server = std::make_shared<BeastHttpServer>(
            ioc,
            tcp::endpoint{address, port},
            backendify_service, // Pass the Backendify service
            logger_,
            config_);
        beast_server->run(); // Start accepting connections

        // Start the frontend server
        if (logger_->getLogLevel() <= LogUtils::LogLevel::DEBUG) {
            ss.str("");
            ss.clear();
            ss << "Starting Frontend server on 0.0.0.0:" << config_.frontend_port << "..." << endl 
            << "Country-Specific Backends Configured: " << config_.country_backend_map.size() << endl
            << "Using Redis cache at " << config_.redis_host << ":" << config_.redis_port;
            logger_->debug(ss.str());
        }

        // --- IOContext Heartbeat Timer ---
        net::steady_timer ioc_heartbeat_timer(ioc);
        std::function<void(beast::error_code)> arm_ioc_heartbeat;
        arm_ioc_heartbeat =
            [&ioc_heartbeat_timer, logger_, &arm_ioc_heartbeat](beast::error_code ec) {
            logger_->debug("[HEARTBEAT_DEBUG] Heartbeat handler entered (std::cerr). EC: " + ec.message());
            if (ec == net::error::operation_aborted) {
                logger_->debug("IOContext Heartbeat timer cancelled (operation_aborted).");
                return;
            }
            logger_->debug("[HEARTBEAT_DEBUG] IOContext Heartbeat Tick.");
            ioc_heartbeat_timer.expires_after(std::chrono::seconds(10)); // Re-arm for 10 seconds
            ioc_heartbeat_timer.async_wait(arm_ioc_heartbeat);
        };
        ioc_heartbeat_timer.expires_after(std::chrono::seconds(10)); // Initial arming
        ioc_heartbeat_timer.async_wait(arm_ioc_heartbeat);
        logger_->setup("[MAIN_DEBUG] IOContext Heartbeat timer started. Backendify Beast Server setup complete. Backendify Beast Server is running on port : " + 
            std::to_string(config_.frontend_port) + ". Press Ctrl+C to exit.");

        // Setup signal handling for graceful shutdown
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait(
            [&](beast::error_code const&, int signal_number) {
                logger_->setup("Signal " + std::to_string(signal_number) + " received. Shutting down...");
                // Stop accepting new server connections
                if (beast_server) beast_server->stop(); // Call the new comprehensive stop
                
                // Cancel active backend client calls
                if (backendify_service) backendify_service->cancel_active_backend_calls();

                // Cancel the heartbeat timer
                ioc_heartbeat_timer.cancel();

                // Signal that the main "work" keeping the io_context alive is done.
                // This allows ioc.run() to exit once all pending handlers complete or are cancelled.
                work_guard.reset(); 
                logger_->setup("[SIGNAL_HANDLER_DEBUG] Work guard reset.");
                
                // Force the io_context to stop dispatching new handlers and unblock all threads
                // currently calling ioc.run(). This is the key to a quick shutdown of the event loop.
                ioc.stop();
                logger_->setup("[SIGNAL_HANDLER_DEBUG] Called ioc.stop().");
            });

        logger_->setup("[MAIN_DEBUG] Main thread calling ioc.run(). This will block until shutdown signal.");
        try {
            logger_->setup("Main thread calling ioc.run().");
            ioc.run();
            logger_->setup("Main thread ioc.run() finished.");
        } catch (const std::exception& e) {
            logger_->error("Exception in main thread ioc.run(): " + std::string(e.what()));
        }
        logger_->setup("[MAIN_DEBUG] Main thread ioc.run() returned.");

        // Ensure ioc is stopped for other threads too if not already (defensive).
        // This defensive stop is likely not needed anymore if the signal handler correctly calls ioc.stop().
        // If ioc.stop() was called by the signal handler, ioc.stopped() will be true here.
        for (auto& t : ioc_threads) {
            logger_->setup("[MAIN_DEBUG] Main thread joining an I/O thread.");
            if (t.joinable()) t.join();
            logger_->setup("[MAIN_DEBUG] An I/O thread has joined.");
        }
        logger_->setup("All Boost.Asio I/O threads joined. Exiting.");
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