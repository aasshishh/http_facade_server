#ifndef BACKENDIFY_HPP
#define BACKENDIFY_HPP

#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>

#include <string>
#include <optional>
#include <unordered_set>

#include "AsyncHttpClientSession.hpp"
#include "CircuitBreaker.hpp"
#include "../config/AppConfig.hpp"
#include "../interfaces/CacheInterface.hpp"
#include "../interfaces/ILogger.hpp"
#include "../interfaces/IStatsDClient.hpp"
#include "../logging/ConsoleLogger.hpp"

#include "../third_party/json.hpp"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

// Forward declarations
class AppConfig;       
class BeastHttpServer;
class CacheInterface; 
class CompanyInfo;

using json = nlohmann::json;

class Backendify {
public:
    Backendify(net::io_context& ioc,
               std::shared_ptr<CacheInterface> cache,
               std::shared_ptr<IStatsDClient> statsd_client,
               const AppConfig& config,
               std::shared_ptr<ILogger> logger)
        : cache_(cache), 
        statsd_client_(statsd_client),
        config_(config),
        logger_(logger) {
        ioc_ = &ioc;
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
    // Old: void setupServer(httplib::Server& server);
    void registerRoutes(BeastHttpServer& server);

    // These methods will be called by HttpServerSession
    void processCompanyRequest(
        http::request<http::string_body> beast_req,
        std::chrono::steady_clock::time_point request_received_time,
        std::function<void(std::optional<http::response<http::string_body>>)> send_response_cb) const;
    void processStatusRequest(std::function<void(std::optional<http::response<http::string_body>>)> send_response_cb) const;
    void cancel_active_backend_calls() const;

private:
    // --- Private Members ---
    std::shared_ptr<CacheInterface> cache_;
    std::shared_ptr<IStatsDClient> statsd_client_;
    net::io_context* ioc_; 
    const AppConfig& config_;
    std::shared_ptr<ILogger> logger_;
    std::unique_ptr<CircuitBreaker> circuit_breaker_;
    mutable std::mutex active_client_sessions_mutex_; 
    mutable std::unordered_set<std::shared_ptr<AsyncHttpClientSession>> active_client_sessions_;

    bool checkCacheAndRespond(const std::string& cache_key, std::function<void(std::optional<http::response<http::string_body>>)> send_response_cb) const;
    const BackendUrlInfo* findBackendInfo(const std::string& country_iso) const;
    void callBackendApi(const BackendUrlInfo* backendUrlInfo,
                        const std::string& company_id,
                        std::function<void(http::response<http::string_body>, beast::error_code)> callback) const;
    void parseBackendResponse(const http::response<http::string_body>& beast_response, CompanyInfo& company_info) const;
    void constructV1Json(const CompanyInfo& company_info, json& final_json_obj) const;
    void constructV2Json(const CompanyInfo& company_info, json& final_json_obj) const;
    void handleBackendServerErrorResponse(int status, const std::string& backendUrl) const;
};

#endif // BACKENDIFY_HPP
