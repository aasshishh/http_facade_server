#include "Backendify.hpp" 

#include <algorithm>
#include <cctype>    
#include <cstdlib>  
#include <cstring>   
#include <iostream> 
#include <map>
#include <memory>    
#include <regex>    
#include <stdexcept>
#include <thread>
#include <utility>

#include "ThreadLocalTime.hpp"
#include "ThreadPoolQueue.hpp"
#include "../models/CompanyInfo.hpp"
#include "../utils/Utils.hpp"

namespace {
    const std::string BACKEND_PATH = "/companies/";
}

// Thread-local storage for HTTP clients. Each thread gets its own map.
// Key: "host:port"
// Value: Client instance for that backend.
thread_local std::map<std::string, std::unique_ptr<httplib::Client>> thread_backend_clients;

// --- Public Interface Method Implementations ---

void Backendify::setupServer(httplib::Server& server) {
    // Configure server to use multiple threads
    const size_t thread_count = std::thread::hardware_concurrency() * config_.number_of_threads_per_core;
    logger_->setup("Configuring thread pool with thread count: " + std::to_string(thread_count));

    // Create the thread pool instance explicitly
    // auto pool = new httplib::ThreadPool(thread_count);

    // Create our custom time-limited queue instance
    auto time_limited_pool = new ThreadPoolQueue(thread_count, logger_);

    // Pre-initialize thread-local clients for each backend in each thread
    logger_->debug("Pre-initializing thread-local backend clients...");
    for (const auto& pair : config_.country_backend_map) {
        const BackendUrlInfo* backendInfoPtr = &pair.second; // Get pointer to BackendUrlInfo
        logger_->debug("  Submitting pre-initialization tasks for backend: " + backendInfoPtr->url);
        for (size_t i = 0; i < thread_count; ++i) {
            time_limited_pool->enqueue([this, backendInfoPtr]() {
                try {
                    get_thread_local_client(backendInfoPtr); // Call the helper to initialize
                } catch (const std::exception& e) {
                    // Log error during pre-initialization, but don't stop server setup
                    logger_->error("Error pre-initializing client for " + backendInfoPtr->url + " in thread: " + e.what());
                }
            });
        }
    }
    logger_->setup("Client pre-initialization tasks submitted.");

    // Assign the created and potentially pre-warmed pool to the server
    server.new_task_queue = [time_limited_pool] {
        return time_limited_pool;
    };

    // Configure /status endpoint handler
    server.Get("/status", [this](const httplib::Request& req, httplib::Response& res) {
        handleStatusRequest(req, res);
    });

    // Configure /company endpoint handler
    server.Get("/company", [this](const httplib::Request& req, httplib::Response& res) {
        handleCompanyRequest(req, res);
    });

    // Configure unknown path endpoint handler
    server.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.path != "/company" && req.path != "/status") {
            logger_->error("Received unhandled GET request for " + req.path);
            res.status = 404; // Not Found
            res.set_content("Not Found", "text/plain");
        }
    });

    logger_->setup("Backendify Server successfully started.");
}

void Backendify::handleCompanyRequest(const httplib::Request& req, httplib::Response& res) const {
    try {
        // -- Read and validate request params ---
        auto id = req.get_param_value("id");
        auto country_iso = req.get_param_value("country_iso");
        std::transform(country_iso.begin(), country_iso.end(), country_iso.begin(), [](unsigned char c) { return std::toupper(c); });

        if (id.empty() || country_iso.empty()) {
            res.status = 400;
            res.set_content(R"({"error": "Missing required parameters"})", "application/json");
            logger_->error("Returning as request is missing required parameters " + id + ":" + country_iso);
            return;
        }
        // --- request validation done

        // --- Check Cache ---
        std::string cache_key = id + ":" + country_iso;
        if (checkCacheAndRespond(cache_key, res)) {
            return;
        }

        // --- Check if request is still active before making Backend Server call ---
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - get_current_request_enqueue_time());
        if (elapsed_time.count() > (config_.server_sla_in_micros - config_.request_average_processing_time_in_micros)) {
            logger_->debug("SLA missed for request. Elapsed time: " + std::to_string(elapsed_time.count()) + "ms. Returning 504.");
            // statsd_client_->increment(MetricsDefinitions::REQUEST_TIMED_OUT);
            res.status = 504; // Gateway Timeout
            res.set_content(R"({"error": "Gateway Timeout - SLA Exceeded"})", "application/json");
            return;
        }
        // --- End IsActive Check ---

        // statsd_client_->increment(MetricsDefinitions::REQUEST_MADE_TO_BACKEND);
        fetchResponseFromBackendServers(res, id, country_iso, cache_key);

    } catch (const std::exception& e) {
        logger_->error("Unexpected exception in handleCompanyRequest: " + std::string(e.what()));
        statsd_client_->increment(MetricsDefinitions::CODE_EXCEPTION);
        res.status = 500;
        res.set_content(R"({"error": "Internal Error"})", "application/json");
    }
}

void Backendify::fetchResponseFromBackendServers(
    httplib::Response& res, 
    std::string& id, 
    std::string& country_iso, 
    std::string& cache_key) const {
        // --- Find Backend Server for requested Country_ISO ---
        const BackendUrlInfo* backendInfoPtr = findBackendInfo(country_iso);
        if (!backendInfoPtr) {
            logger_->error("Unconfigured Country : " + country_iso);
            res.status = 404;
            res.set_content(R"({"error": "Unconfigured country_iso"})", "application/json");
            return;
        }
        const BackendUrlInfo& backendUrlInfo = *backendInfoPtr; // Dereference the valid pointer
        
        // --- Simple Circuit Breaker Check ---
        if (circuit_breaker_->isTripped(backendUrlInfo.url)) {
            res.status = 504;
            res.set_content(R"({"error": "Gateway Timeout - Circuit Breaker Active"})", "application/json");
            return;
        }
        // --- End Circuit Breaker Check ---

        // --- Call Backend API with Retry ---
        httplib::Result backend_response{nullptr, httplib::Error::Unknown}; // Initialize with an error state
        int max_retries = 0; // Set to 1 to allow one retry
        for (int attempt = 0; attempt <= max_retries; ++attempt) {
            backend_response = callBackendApi(&backendUrlInfo, id);

            // Check if the call was successful or resulted in a non-retryable error
            if (backend_response || (backend_response.error() != httplib::Error::Connection &&
                                      backend_response.error() != httplib::Error::Read &&
                                      backend_response.error() != httplib::Error::Write &&
                                      backend_response.error() != httplib::Error::SSLConnection &&
                                      backend_response.error() != httplib::Error::Unknown)) {
                break; // Success or non-retryable error, exit loop
            }

            // Log retry attempt if error is potentially transient
            if (attempt < max_retries) {
                logger_->error("Backend call failed (attempt " + std::to_string(attempt + 1) + "), retrying...");
            }
        }
        // --- End Call Backend API with Retry ---

        // --- Process Backend Response ---
        if (!backend_response) {
            // statsd_client_->increment(MetricsDefinitions::BACKEND_NO_RESPONSE_ERROR);
            
            // log error
            auto err = backend_response.error();
            stringstream ss;
            ss << "Error calling Backend API: " << httplib::to_string(err) << " (Code: " << static_cast<int>(err) << ")" << " for companyId : " << id;
            logger_->warn(ss.str());

            // prepare resposne
            res.status = 504;
            res.set_content(R"({"error": "Gateway Timeout"})", "application/json");
            return;
        }

        if (backend_response->status == 200) {
            CompanyInfo company_info;
            company_info.id = id;
            parseBackendResponse(backend_response, company_info);
            json final_json_obj; // Use nlohmann::json object

            if (company_info.parse_success) {
                if (company_info.version == 1) {
                    constructV1Json(company_info, final_json_obj);
                } else if (company_info.version == 2) {
                    constructV2Json(company_info, final_json_obj);
                }

                std::string final_json_string = final_json_obj.dump(4); // Serialize with indentation
                res.status = 200; // OK
                res.set_content(final_json_string, "application/json");

                // --- Cache the successful response ---
                cache_->set(cache_key, final_json_string, 3600 * 24); // Cache for 24 hours
                if (logger_->getLogLevel() <= LogUtils::LogLevel::DEBUG) {
                    stringstream ss;
                    ss << "Setting cache for Key : " << cache_key << " , value : " << final_json_string;
                    logger_->debug(ss.str());
                }
                // --- End Cache ---
            } else {
                // JSON parsing failed although backend returned 200 - forward the original response
                logger_->error("Backend returned 200 but response parsing failed for companyId: " + id);
                res.status = 502;
                res.set_content(R"({"error": "Bad Gateway"})", "application/json");
                // statsd_client_->increment(MetricsDefinitions::BACKEND_RESPONSE_PARSE_ERROR);
            }
        } else if (backend_response->status == 404) {
            logger_->debug("Data requested was not found. Expected Error. Returning 404");
            res.status = 404;
            res.set_content(R"({"error": "Not Found"})", "application/json");
            // statsd_client_->increment(MetricsDefinitions::BACKEND_RESPONSE_404); // Metric might be noisy
        } else if (backend_response->status >= 500 && backend_response->status < 600) {
            // Handle any 5xx "Server Error" from the backend
            handleBackendServerErrorResponse(backend_response->status, backendUrlInfo.url);
            res.status = 504;
            res.set_content(R"({"error": "Gateway Timeout"})", "application/json");
        } else {
            // Backend returned an error status other than 404 and 5XX
            logger_->error("Backend API returned error status: " + 
                std::to_string(backend_response->status) + " for companyId: " + id);
            // statsd_client_->increment(MetricsDefinitions::BACKEND_RESPONSE_STATUS_CODE_ERROR);
            res.status = 502;
            res.set_content(R"({"error": "Bad Gateway"})", "application/json");
        }
}

void Backendify::handleStatusRequest(const httplib::Request& /*req*/, httplib::Response& res) { 
    res.status = 200;
    res.set_content("Frontend Server is running", "text/plain");
}

// --- Private Helper Method Implementations ---

// Helper to handle actions when a backend returns a 5xx error
void Backendify::handleBackendServerErrorResponse(int status, const std::string& backendUrl) const {
    logger_->error("Backend returned "
        + std::to_string(status)
        + " for "
        + backendUrl
        + ". Tripping circuit breaker for "
        + std::to_string(config_.backend_servers_circuit_breaker_cool_off_duration_in_millis)
        + "ms.");

    statsd_client_->increment(MetricsDefinitions::CIRCUIT_BREAKER_LOGGED);
    circuit_breaker_->trip(backendUrl,
        std::chrono::milliseconds(config_.backend_servers_circuit_breaker_cool_off_duration_in_millis));
}

bool Backendify::checkCacheAndRespond(const std::string& cache_key, httplib::Response& res) const {
    auto cached_response = cache_->get(cache_key);
    if (cached_response) {
        logger_->debug("Found cache for key : " + cache_key);
        res.status = 200;
        res.set_content(*cached_response, "application/json");
        return true; // Indicate cache hit and response set
    }
    return false; // Indicate cache miss
}

// --- Find Backend Info Helper ---
const BackendUrlInfo* Backendify::findBackendInfo(const std::string& country_iso) const {
    auto it = config_.country_backend_map.find(country_iso);
    if (it == config_.country_backend_map.end()) {
        return nullptr; // Not found
    }
    return &(it->second); // Return pointer to the found info
}

// Helper to get or create a client for a specific backend within the current thread
httplib::Client* Backendify::get_thread_local_client(const BackendUrlInfo* backendUrlInfo) const {
    std::string client_key = backendUrlInfo->backend_host + ":" + std::to_string(backendUrlInfo->backend_port);

    auto it = thread_backend_clients.find(client_key);
    if (it == thread_backend_clients.end()) {
        // Client not found for this thread and backend, create it
        std::unique_ptr<httplib::Client> new_client;
        if (backendUrlInfo->is_https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            auto ssl_client = std::make_unique<httplib::SSLClient>(backendUrlInfo->backend_host.c_str(), backendUrlInfo->backend_port);
            // Configure SSL options if needed, e.g., certificate verification
            // ssl_client->enable_server_certificate_verification(true);
            new_client = std::move(ssl_client); // Assign SSLClient to Client unique_ptr
#else
            // Handle case where SSL support is needed but not compiled in
            throw std::runtime_error("HTTPS requested but SSL support not available for backend: " + backendUrlInfo->url);
#endif
        } else {
            new_client = std::make_unique<httplib::Client>(backendUrlInfo->backend_host.c_str(), backendUrlInfo->backend_port);
        }

        // Configure the new client (timeouts, compression, etc.)
        new_client->set_connection_timeout(0, config_.connection_timeout_in_microseconds);
        new_client->set_read_timeout(0, config_.read_request_timeout_in_microseconds);
        new_client->set_compress(true);           // Enable compression

        auto [inserted_it, success] = thread_backend_clients.emplace(client_key, std::move(new_client));
        it = inserted_it;
    }
    return it->second.get(); // Return raw pointer to the client managed by unique_ptr
}

httplib::Result Backendify::callBackendApi(const BackendUrlInfo* backendUrlInfo, const std::string& company_id) const {
    try {
        // log timestamp
        auto startTime = std::chrono::steady_clock::now().time_since_epoch();
        httplib::Client* client = dynamic_cast<httplib::Client*>(get_thread_local_client(backendUrlInfo));
        if (client) {
            std::string backend_path = BACKEND_PATH + company_id;
            // Use the thread-local client
            return client->Get(backend_path.c_str());
        } else {
            // This case should ideally not happen if get_thread_local_client throws on error
            logger_->error("Failed to get or create thread-local client for backend: " + backendUrlInfo->url);
            // Return an error Result, using Canceled as Canceled won't be retried
            return httplib::Result{nullptr, httplib::Error::Canceled};
        }
    } catch (const std::exception& e) {
        // Catch exceptions during client creation/retrieval (e.g., SSL not supported)
        logger_->error("Exception while getting/creating client for " + backendUrlInfo->url + ": " + e.what());
        // Return an error Result
        return httplib::Result{nullptr, httplib::Error::Unknown};
    }
 }

void Backendify::parseBackendResponse(const httplib::Result& result, CompanyInfo& info) const {
    try {
        std::string contentType = result->get_header_value("Content-Type");
        json bodyJson = json::parse(result->body);
        if (contentType == "application/x-company-v1") {
            info.version = 1;
            info.parse_success = true;
            info.name = bodyJson.value("cn", "");
            info.created_on = bodyJson.value("created_on", "");
            // Safely get optional values
             if (bodyJson.contains("closed_on") && bodyJson["closed_on"].is_string()) {
                info.closed_on = bodyJson["closed_on"].get<std::string>();
            }
        } else if (contentType == "application/x-company-v2") {
            info.version = 2;
            info.parse_success = true;
            info.name = bodyJson.value("company_name", "");
            info.tin = bodyJson.value("tin", "");
             // Safely get optional values
             if (bodyJson.contains("dissolved_on") && bodyJson["dissolved_on"].is_string()) {
                info.dissolved_on = bodyJson["dissolved_on"].get<std::string>();
            }
        }
    } catch (json::parse_error& e) {
        stringstream ss;
        ss << "Backend response JSON parse error: " << e.what() << "\nBody: " << result->body;
        logger_->error(ss.str());
    } catch (json::type_error& e) {
        stringstream ss;
        ss << "Backend response JSON type error: " << e.what() << "\nBody: " << result->body;
        logger_->error(ss.str());
    } catch (const std::exception& e) {
        stringstream ss;
        ss << "Unexpected error during backend response parsing: " << e.what();
        logger_->error(ss.str());
    }
}

void Backendify::constructV1Json(const CompanyInfo& company_info, json& final_json_obj) const {
    final_json_obj["id"] = company_info.id;
    final_json_obj["name"] = company_info.name;
    final_json_obj["active"] = true;
    if (company_info.created_on 
        && !company_info.created_on->empty() 
        && Utils::isUTCTimeInFuture(company_info.created_on.value())) {
        final_json_obj["active"] = false;
    }
    if (company_info.closed_on && !company_info.closed_on->empty()) {
        if (Utils::isUTCTimeInFuture(company_info.closed_on.value())) {
            final_json_obj["active_until"] = company_info.closed_on.value();
        } else {       
            final_json_obj["active"] = false;
            final_json_obj["active_until"] = company_info.closed_on.value();
        }
    }     
}

void Backendify::constructV2Json(const CompanyInfo& company_info, json& final_json_obj) const {
    final_json_obj["id"] = company_info.id;
    final_json_obj["name"] = company_info.name;
    final_json_obj["active"] = true;
    if (company_info.dissolved_on && !company_info.dissolved_on->empty()) {
        if (Utils::isUTCTimeInFuture(company_info.dissolved_on.value())) {
            final_json_obj["active_until"] = company_info.dissolved_on.value();
        } else {
            final_json_obj["active"] = false;
            final_json_obj["active_until"] = company_info.dissolved_on.value();
        }
    }
}