#include "Backendify.hpp" 

#include <future> // For std::promise and std::future
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

#include "AsyncHttpClientSession.hpp" // Our new async client
#include "../models/CompanyInfo.hpp"
#include "../utils/Utils.hpp"

namespace {
    const std::string BACKEND_PATH = "/companies/";
}

// --- Public Interface Method Implementations ---
void Backendify::registerRoutes(BeastHttpServer& /*server*/) {
    // With Beast, routing is typically handled within the HttpServerSession
    // or by a dedicated router class that HttpServerSession calls.
    // Backendify itself doesn't directly interact with server.Get() anymore.
    // The HttpServerSession::handle_request will dispatch to Backendify methods.
    logger_->setup("Backendify Server successfully started.");
}

void Backendify::processCompanyRequest(
    http::request<http::string_body> beast_req,
    std::chrono::steady_clock::time_point request_received_time,
    std::function<void(std::optional<http::response<http::string_body>>)> send_response_cb
) const {
    logger_->debug("Received /company request");
    http::response<http::string_body> beast_res; // Prepare response object
    beast_res.version(beast_req.version());
    beast_res.keep_alive(beast_req.keep_alive());
    beast_res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    beast_res.set(http::field::content_type, "application/json");

    try {
        // Extract query parameters from beast_req.target()
        // Example: /company?id=123&country_iso=US
        std::string target_str(beast_req.target());
        std::string query_params_str;
        size_t query_pos = target_str.find('?');
        if (query_pos != std::string::npos) {
            query_params_str = target_str.substr(query_pos + 1);
        }

        std::map<std::string, std::string> params;
        std::string current_param;
        std::string key, value;
        bool parsing_key = true;
        for (char c : query_params_str) {
            if (c == '=') {
                key = Utils::urlDecode(current_param);
                current_param.clear();
                parsing_key = false;
            } else if (c == '&') {
                value = Utils::urlDecode(current_param);
                if (!key.empty()) params[key] = value;
                current_param.clear();
                key.clear(); value.clear();
                parsing_key = true;
            } else {
                current_param += c;
            }
        }
        if (!current_param.empty()) {
             if (parsing_key) key = Utils::urlDecode(current_param); // if only key like ?id
             else value = Utils::urlDecode(current_param);
             if (!key.empty()) params[key] = value;
        }

        // -- Read and validate request params ---
        std::string id = params.count("id") ? params.at("id") : "";
        std::string country_iso = params.count("country_iso") ? params.at("country_iso") : "";
        std::transform(country_iso.begin(), country_iso.end(), country_iso.begin(), [](unsigned char c) { return std::toupper(c); });

        if (id.empty() || country_iso.empty()) {
            beast_res.result(http::status::bad_request);
            beast_res.body() = R"({"error": "Missing required parameters"})";
            beast_res.prepare_payload();
            logger_->error("Returning as request is missing required parameters " + id + ":" + country_iso);
            return send_response_cb(std::make_optional(std::move(beast_res)));
        }
        // --- request validation done

        // --- Check Cache ---
        std::string cache_key = id + ":" + country_iso;
        if (checkCacheAndRespond(cache_key, send_response_cb)) {
            return; // Response sent by cache handler
        }

        // --- Check if request is still active before making Backend Server call ---
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - request_received_time);
        if (elapsed_time.count() > (config_.server_sla_in_micros - config_.request_average_processing_time_in_micros)) { // Potential SLA miss
            if (config_.drop_sla_timeout_requests) {
                logger_->warn("SLA missed for request. Elapsed time: " + std::to_string(elapsed_time.count()) + "micros. Dropping request as per configuration.");
                statsd_client_->increment(MetricsDefinitions::REQUEST_TIMED_OUT); // Consider a new metric
                return send_response_cb(std::nullopt); // Signal to drop
            } else {
                logger_->warn("SLA missed for request. Elapsed time: " + std::to_string(elapsed_time.count()) + "micros. Returning 504.");
                statsd_client_->increment(MetricsDefinitions::REQUEST_TIMED_OUT);
                beast_res.result(http::status::gateway_timeout);
                beast_res.body() = R"({"error": "Gateway Timeout - SLA Exceeded"})";
                beast_res.prepare_payload();
                return send_response_cb(std::make_optional(std::move(beast_res)));
            }
        }
        // --- End IsActive Check ---

        // --- Find Backend Server for requested Country_ISO ---
        const BackendUrlInfo* backendInfoPtr = findBackendInfo(country_iso);
        if (!backendInfoPtr) {
            logger_->error("Unconfigured Country : " + country_iso);
            beast_res.result(http::status::not_found);
            beast_res.body() = R"({"error": "Unconfigured country_iso"})";
            beast_res.prepare_payload();
            return send_response_cb(std::make_optional(std::move(beast_res)));
        }
        const BackendUrlInfo& backendUrlInfo = *backendInfoPtr; // Dereference the valid pointer
        
        // --- Simple Circuit Breaker Check ---
        if (circuit_breaker_->isTripped(backendUrlInfo.url)) {
            beast_res.result(http::status::gateway_timeout);
            beast_res.body() = R"({"error": "Gateway Timeout - Circuit Breaker Active"})";
            beast_res.prepare_payload();
            return send_response_cb(std::make_optional(std::move(beast_res)));
        }
        // --- End Circuit Breaker Check ---

        callBackendApi(&backendUrlInfo, id,
            // Completion handler for the asynchronous backend call:
            [this, beast_res_cb = std::move(beast_res), send_response_cb_copy = send_response_cb, cache_key_copy = cache_key, id_copy = id, backend_url_copy = backendUrlInfo.url] 
            (http::response<http::string_body> backend_api_response, beast::error_code ec) mutable {
                // This lambda is executed by a Boost.Asio IO_Thread.
                if (ec && ec != http::error::end_of_stream) {
                    logger_->error("Error calling Backend API (Boost.Asio): " + ec.message() + " for companyId : " + id_copy);
                    beast_res_cb.result(http::status::gateway_timeout);
                    beast_res_cb.body() = R"({"error": "Gateway Timeout - Backend Unreachable"})";
                    if (ec == net::error::connection_refused || ec == beast::errc::timed_out) {
                        handleBackendServerErrorResponse(503, backend_url_copy); // Trip circuit breaker
                    }
                } else {
                    if (backend_api_response.result_int() == 200) {
                        CompanyInfo company_info;
                        company_info.id = id_copy;
                        parseBackendResponse(backend_api_response, company_info);
                        if (company_info.parse_success) {
                            json final_json_obj;
                            if (company_info.version == 1) constructV1Json(company_info, final_json_obj);
                            else if (company_info.version == 2) constructV2Json(company_info, final_json_obj);
                            
                            std::string final_json_string = final_json_obj.dump(4);
                            beast_res_cb.result(http::status::ok);
                            beast_res_cb.body() = final_json_string;
                            cache_->set(cache_key_copy, final_json_string, 3600 * 24);
                            logger_->debug("Setting cache for Key : " + cache_key_copy);
                        } else {
                            logger_->error("Backend returned 200 but response parsing failed for companyId: " + id_copy + " <body> : " + backend_api_response.body());
                            beast_res_cb.result(http::status::bad_gateway);
                            beast_res_cb.body() = R"({"error": "Bad Gateway - Upstream Response Parse Error"})";
                        } 
                    } else if (backend_api_response.result_int() == 404) {
                        logger_->debug("Data requested was not found from backend. CompanyId: " + id_copy);
                        beast_res_cb.result(http::status::not_found);
                        beast_res_cb.body() = R"({"error": "Not Found from backend"})";
                    } else if (backend_api_response.result_int() >= 500 && backend_api_response.result_int() < 600) {
                        handleBackendServerErrorResponse(backend_api_response.result_int(), backend_url_copy);
                        beast_res_cb.result(http::status::bad_gateway); // Or gateway_timeout
                        beast_res_cb.body() = R"({"error": "Bad Gateway - Upstream Server Error"})";
                    } else {
                        statsd_client_->increment(MetricsDefinitions::CODE_EXCEPTION);
                        logger_->error("Backend API returned unhandled status: " + std::to_string(backend_api_response.result_int()) + " for companyId: " + id_copy);
                        beast_res_cb.result(http::status::bad_gateway);
                        beast_res_cb.body() = R"({"error": "Bad Gateway - Unknown Upstream Status"})";
                    }
                }
                beast_res_cb.prepare_payload();
                send_response_cb_copy(std::make_optional(std::move(beast_res_cb)));
            });
    } catch (const std::exception& e) {
        logger_->error("Unexpected exception in processCompanyRequest: " + std::string(e.what()));
        statsd_client_->increment(MetricsDefinitions::CODE_EXCEPTION);
        beast_res.result(http::status::internal_server_error);
        beast_res.body() = R"({"error": "Internal Server Error"})";
        beast_res.prepare_payload();
        send_response_cb(std::make_optional(std::move(beast_res)));
    }
}

void Backendify::processStatusRequest(std::function<void(std::optional<http::response<http::string_body>>)> send_response_cb) const {
    http::response<http::string_body> res;
    logger_->debug("Received /status request");
    res.set(http::field::content_type, "text/plain");
    res.result(http::status::ok);
    res.body() = "Frontend Server is running (Beast)";
    res.prepare_payload();
    logger_->debug("Prepared /status response. Body: " + res.body() + ". Calling send_response_cb.");
    send_response_cb(std::make_optional(std::move(res)));
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

bool Backendify::checkCacheAndRespond(const std::string& cache_key, std::function<void(std::optional<http::response<http::string_body>>)> send_response_cb) const {
    auto cached_response_str = cache_->get(cache_key);
    if (cached_response_str) {
        logger_->debug("Found cache for key : " + cache_key);
        http::response<http::string_body> res;
        res.version(11);
        res.keep_alive(true); // Assuming keep-alive for cached responses
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        res.result(http::status::ok);
        res.body() = *cached_response_str;
        res.prepare_payload();
        send_response_cb(std::make_optional(std::move(res)));
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

void Backendify::callBackendApi(
    const BackendUrlInfo* backendUrlInfo,
    const std::string& company_id,
    std::function<void(http::response<http::string_body>, beast::error_code)> callback) const {
    if (!backendUrlInfo) {
        logger_->error("callBackendApi: backendUrlInfo is null for company_id: " + company_id);
        net::post(*ioc_, [cb = std::move(callback)]() {
            cb({}, beast::errc::make_error_code(beast::errc::invalid_argument));
        });
        return;
    }

    if (backendUrlInfo->is_https) {
         logger_->error("HTTPS not yet supported in callBackendApi for: " + backendUrlInfo->url);
         net::post(*ioc_, [cb = std::move(callback)]() {
            cb({}, beast::errc::make_error_code(beast::errc::operation_not_supported));
        });
        return;
    }

    std::string target_path = BACKEND_PATH + company_id;
    auto timeout_ms = std::chrono::milliseconds( (config_.connection_timeout_in_microseconds + config_.read_request_timeout_in_microseconds) / 1000 + 200); // Convert and add buffer

    auto client_session_sp = std::make_shared<AsyncHttpClientSession>(
        *ioc_, 
        *backendUrlInfo, 
        target_path, 
        timeout_ms, 
        // This is the on_complete_ handler for AsyncHttpClientSession
        [this, original_user_callback = std::move(callback), session_to_remove_on_complete = std::weak_ptr<AsyncHttpClientSession>()] 
        (http::response<http::string_body> res, beast::error_code ec) mutable {
            // When AsyncHttpClientSession calls this, try to lock the weak_ptr.
            // If successful, it means the session object still exists and we can remove it.
            if (auto strong_session_ptr = session_to_remove_on_complete.lock()) {
                 std::lock_guard<std::mutex> lock(active_client_sessions_mutex_);
                 active_client_sessions_.erase(strong_session_ptr);
                 logger_->debug("AsyncHttpClientSession removed from tracking set.");
            }
            // Call the original user's callback
            if (original_user_callback) {
                original_user_callback(std::move(res), ec);
            }
        },
        logger_
    );

    // Add to active sessions
    {
        std::lock_guard<std::mutex> lock(active_client_sessions_mutex_);
        active_client_sessions_.insert(client_session_sp);
    }
    
    client_session_sp->run();
}

void Backendify::parseBackendResponse(const http::response<http::string_body>& beast_response, CompanyInfo& info) const {
    try {
        std::string contentType;
        if(beast_response.count(http::field::content_type)) {
            contentType = std::string(beast_response[http::field::content_type]);
        }
        if (beast_response.body().empty()) {
            return;
        }
        json bodyJson = json::parse(beast_response.body());

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
        statsd_client_->increment(MetricsDefinitions::JSON_ERROR);
        stringstream ss;
        ss << "Backend response JSON parse error: " << e.what() << "\nBody: " << beast_response.body();
        logger_->error(ss.str());
    } catch (json::type_error& e) {
        statsd_client_->increment(MetricsDefinitions::JSON_ERROR);
        stringstream ss;
        ss << "Backend response JSON type error: " << e.what() << "\nBody: " << beast_response.body();
        logger_->error(ss.str());
    } catch (const std::exception& e) {
        statsd_client_->increment(MetricsDefinitions::CODE_EXCEPTION);
        stringstream ss;
        ss << "Unexpected error during backend response parsing: " << e.what() << "\nBody: " << beast_response.body();
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

void Backendify::cancel_active_backend_calls() const {
    logger_->info("Backendify cancelling active backend calls...");
    
    // Create a temporary vector of shared_ptrs to call cancel on.
    // This avoids holding the mutex while calling cancel(), as cancel() might
    // trigger the completion handler which in turn tries to lock the same mutex.
    std::vector<std::shared_ptr<AsyncHttpClientSession>> sessions_to_cancel_vec;
    {
        std::lock_guard<std::mutex> lock(active_client_sessions_mutex_);
        if (active_client_sessions_.empty()) {
            logger_->info("No active backend calls to cancel.");
            return;
        }
        logger_->info("Found " + std::to_string(active_client_sessions_.size()) + " active backend calls to attempt cancellation.");
        sessions_to_cancel_vec.reserve(active_client_sessions_.size());
        for (const auto& session_ptr : active_client_sessions_) {
            sessions_to_cancel_vec.push_back(session_ptr);
        }
    } // Mutex is released here

    for (const auto& session_ptr : sessions_to_cancel_vec) {
        if (session_ptr) {
            // logger_->debug("Cancelling an AsyncHttpClientSession...");
            session_ptr->cancel(); // This should trigger its completion handler
        }
    }
    // The sessions will remove themselves from active_client_sessions_ via their completion handlers.
}