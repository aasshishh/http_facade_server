#ifndef BACKENDIFY_HPP
#define BACKENDIFY_HPP

#include <memory>
#include <string>

#include "CircuitBreaker.hpp"
#include "../config/AppConfig.hpp"
#include "../interfaces/CacheInterface.hpp"
#include "../interfaces/ILogger.hpp"
#include "../interfaces/IStatsDClient.hpp"
#include "../logging/ConsoleLogger.hpp"
#include "../metrics/DummyStatsDClient.hpp"
#include "../metrics/StatsDClient.hpp"
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

// Forward declarations
class AppConfig;    
class CacheInterface;
class CompanyInfo;

using json = nlohmann::json;

class Backendify {
public:
    Backendify(std::shared_ptr<CacheInterface> cache,
               std::shared_ptr<IStatsDClient> statsd_client,
               const AppConfig& config,
               std::shared_ptr<ILogger> logger)
        : cache_(cache), 
        statsd_client_(statsd_client),
        config_(config),
        logger_(logger) {
        if (!cache_) {
            throw std::invalid_argument("Cache pointer cannot be null");
        }
        if (!statsd_client_) {
            throw std::invalid_argument("StatsDClient pointer cannot be null");
        }
        if (!logger_) {
            throw std::invalid_argument("Logger pointer cannot be null");
        }
        circuit_breaker_ = std::make_unique<CircuitBreaker>(logger_, statsd_client_);
        logger_->debug("Backendify initialized");
    }

    virtual ~Backendify() = default;

    // Delete copy/move operations to prevent slicing and manage ownership correctly
    Backendify(const Backendify&) = delete;
    Backendify& operator=(const Backendify&) = delete;
    Backendify(Backendify&&) = delete;
    Backendify& operator=(Backendify&&) = delete;

    // --- Public Interface Methods ---
    void setupServer(httplib::Server& server);
    void handleCompanyRequest(const httplib::Request& req, httplib::Response& res) const;
    void handleStatusRequest(const httplib::Request& req, httplib::Response& res);

private:
    // --- Private Members ---
    std::shared_ptr<CacheInterface> cache_;
    std::shared_ptr<IStatsDClient> statsd_client_;
    const AppConfig& config_;
    std::shared_ptr<ILogger> logger_;
    std::unique_ptr<CircuitBreaker> circuit_breaker_;

    bool checkCacheAndRespond(const std::string& cache_key, httplib::Response& res) const;
    const BackendUrlInfo* findBackendInfo(const std::string& country_iso) const;
    httplib::Result callBackendApi(const BackendUrlInfo* backendUrlInfo, const std::string& company_id) const;
    void parseBackendResponse(const httplib::Result& result, CompanyInfo& company_info) const;
    void constructV1Json(const CompanyInfo& company_info, json& final_json_obj) const;
    void constructV2Json(const CompanyInfo& company_info, json& final_json_obj) const;
    httplib::Client* get_thread_local_client(const BackendUrlInfo* backendUrlInfo) const;
    void handleBackendServerErrorResponse(int status, const std::string& backendUrl) const;
    void fetchResponseFromBackendServers(
        httplib::Response& res, 
        std::string& id, 
        std::string& country_iso, 
        std::string& cache_key) const;
};

#endif // BACKENDIFY_HPP
