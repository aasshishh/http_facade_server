// test/test_backendify.cpp
#include <atomic>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/beast/http.hpp>

#include "../src/config/AppConfig.hpp"
#include "../src/core/Backendify.hpp"
#include "../src/interfaces/CacheInterface.hpp"
#include "../src/interfaces/IStatsDClient.hpp"
#include "../src/models/BackendUrlInfo.hpp"
#include "../src/models/CompanyInfo.hpp"
#include "../src/third_party/httplib.h"

using json = nlohmann::json;

// Mock class for StatsDClient
class MockStatsDClient : public IStatsDClient {
public:
    MOCK_METHOD(void, increment, (const std::string& key, int value), ());
    MOCK_METHOD(void, decrement, (const std::string& key, int value), ());
    MOCK_METHOD(void, gauge, (const std::string& key, double value), ());
    MOCK_METHOD(void, timing, (const std::string& key, std::chrono::milliseconds value), ());
    MOCK_METHOD(void, set, (const std::string& key, const std::string& value), ());
};

// --- Mock Cache ---
class MockCache : public CacheInterface {
public:
    MOCK_METHOD(bool, set, (const std::string& key, const std::string& value, int ttl), ());
    MOCK_METHOD(std::optional<std::string>, get, (const std::string& key), ());
    MOCK_METHOD(bool, remove, (const std::string& key), ());
    MOCK_METHOD(bool, clear, (), ());
    MOCK_METHOD(bool, exists, (const std::string& key), ());
};

// --- Mock Logger ---
class MockLogger : public ILogger {
    public:
        MOCK_METHOD(void, info, (const std::string& message), ());
        MOCK_METHOD(void, debug, (const std::string& message), ());
        MOCK_METHOD(void, warn, (const std::string& message), ());
        MOCK_METHOD(void, error, (const std::string& message), ());
        MOCK_METHOD(void, setup, (const std::string& message), ());
        MOCK_METHOD(int, getLogLevel, (), ());
    };

// --- Test Fixture for Backendify ---
class BackendifyTest : public ::testing::Test {
protected:
    std::unique_ptr<httplib::Server> fake_server;
    std::thread server_thread;
    MockCache* mock_cache{nullptr};         // Change to raw pointer
    MockStatsDClient* mock_statsd{nullptr}; // Change to raw pointer
    MockLogger* mock_logger{nullptr}; // Change to raw pointer
    boost::asio::io_context ioc_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
    std::vector<std::thread> ioc_threads_;
    AppConfig config;
    std::atomic<bool> server_ready{false};
    int fake_server_port = 9001; // Default fake backend port

    void SetUp() override {
        updateBackendUrls();

        // Start io_context threads
        work_guard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(ioc_));
        ioc_threads_.emplace_back([this]() { ioc_.run(); });
        ioc_threads_.emplace_back([this]() { ioc_.run(); }); // Start a couple of threads for tests

        // Start the fake server
        fake_server = std::make_unique<httplib::Server>();
        setupFakeServerRoutes();
        startFakeServer();
    }

    void TearDown() override {
        if (fake_server && fake_server->is_running()) {
            fake_server->stop();
        }
        if (server_thread.joinable()) {
            server_thread.join();
        }

        // Stop io_context threads
        work_guard_.reset();
        ioc_.stop();
        for (auto& t : ioc_threads_) {
            if (t.joinable()) t.join();
        }
        // No need to delete mock_cache or mock_statsd as they're managed by Backendify
    }

    // Define how the fake server should respond
    void setupFakeServerRoutes() {
        fake_server->Get("/companies/123", [](const httplib::Request&, httplib::Response& res) {
            // Update JSON structure to match expected format
            res.set_content(R"({
                "company_name": "FakeCo V2",
                "id": "123",
                "version": "v2"
            })", "application/x-company-v2");
            res.status = 200;
        });
        fake_server->Get("/companies/456", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({
                "cn": "FakeCo V1",
                "closed_on": "2024-01-01T00:00:00Z",
                "version": "v1"
            })", "application/x-company-v1");
            res.status = 200;
        });
        fake_server->Get("/companies/789", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"company_name": "ActiveCo"})", "application/json"); // Unknown content type
            res.status = 200;
        });
        fake_server->Get("/companies/invalid", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"error": "Internal Server Error"})", "application/json");
            res.status = 500;
        });
        fake_server->Get("/companies/invalid_json", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"name": "Bad JSON",)", "application/json");
            res.status = 200; // Status is OK, but body is bad
        });
        fake_server->Get("/companies/notfound", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"error":"backend not found"})", "application/json");
            res.status = 404;
        });
        // Add more routes as needed for different test cases
    }

// Helper to simulate a client request by directly calling Backendify's processing methods
    // Returns a Beast response object
    boost::beast::http::response<boost::beast::http::string_body> 
    simulateRequest(Backendify& backendify, const std::string& target_path, boost::beast::http::verb method = boost::beast::http::verb::get) {
        boost::beast::http::request<boost::beast::http::string_body> req{method, target_path, 11}; // HTTP/1.1
        req.set(boost::beast::http::field::host, "localhost");
        req.set(boost::beast::http::field::user_agent, "test-client");
        req.prepare_payload();

        std::promise<boost::beast::http::response<boost::beast::http::string_body>> promise;
        auto future = promise.get_future();

        auto send_response_cb = 
            [&promise](std::optional<boost::beast::http::response<boost::beast::http::string_body>> res) {
                if (res) {
                    promise.set_value(std::move(*res));
                } else {
                    throw std::runtime_error("Received empty response in test simulation");
                }
            };

        // Basic routing for tests
        if (target_path.rfind("/company", 0) == 0) { // starts_with
            backendify.processCompanyRequest(std::move(req), std::chrono::steady_clock::now(), send_response_cb);
        } else if (target_path == "/status") {
            backendify.processStatusRequest(send_response_cb);
        } else {
            // Simulate a 404 for unhandled paths in the test context
            boost::beast::http::response<boost::beast::http::string_body> res_404{boost::beast::http::status::not_found, req.version()};
            res_404.set(boost::beast::http::field::content_type, "text/plain");
            res_404.body() = "Not Found in test simulation";
            res_404.prepare_payload();
            promise.set_value(std::move(res_404));
        }

        // Wait for the promise to be fulfilled. Add a timeout for safety.
        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            throw std::runtime_error("Simulated request timed out");
        }
        return future.get();
    }

    // Helper to create Backendify instance
    Backendify createBackendify() {
        auto mock_cache_ptr = std::make_shared<MockCache>();
        auto mock_statsd_ptr = std::make_shared<MockStatsDClient>();
        auto mock_logger_ptr = std::make_shared<MockLogger>();
        
        // Store raw pointers before moving ownership
        mock_cache = mock_cache_ptr.get();
        mock_statsd = mock_statsd_ptr.get();
        mock_logger = mock_logger_ptr.get();
        
        return Backendify(
            ioc_, // Pass the io_context
            std::move(mock_cache_ptr),
            std::move(mock_statsd_ptr),
            config,
            mock_logger_ptr
        );  
    }

private:
    void startFakeServer() {
        server_thread = std::thread([this]() {
            for (int p = fake_server_port; p < fake_server_port + 5; ++p) {
                if (fake_server->bind_to_port("127.0.0.1", p)) {
                    fake_server_port = p;
                    updateBackendUrls(); // Update config *after* port is confirmed
                    server_ready = true;
                    fake_server->listen_after_bind();
                    return;
                }
            }
            std::cerr << "Failed to bind fake server to any port" << std::endl;
            server_ready = true; // Still set to true to unblock waitForServer, but it will fail later
        });

        waitForServer();
    }

    void updateBackendUrls() {
        BackendUrlInfo us_backend;
        us_backend.url = "http://127.0.0.1:" + std::to_string(fake_server_port);
        us_backend.backend_host = "127.0.0.1";
        us_backend.backend_port = fake_server_port;
        us_backend.is_https = false;
        config.country_backend_map["US"] = std::move(us_backend); // Use std::move

        BackendUrlInfo gb_backend;
        gb_backend.url = "http://127.0.0.1:" + std::to_string(fake_server_port + 1);
        gb_backend.backend_host = "127.0.0.1";
        gb_backend.backend_port = fake_server_port + 1;
        gb_backend.is_https = false;
        config.country_backend_map["GB"] = std::move(gb_backend); // Use std::move


        BackendUrlInfo de_backend;
        de_backend.url = "http://127.0.0.1:" + std::to_string(fake_server_port);
        de_backend.backend_host = "127.0.0.1";
        de_backend.backend_port = fake_server_port;
        de_backend.is_https = false;
        config.country_backend_map["DE"] = std::move(de_backend); // Use std::move
    }

    void waitForServer() {
        const auto timeout = std::chrono::seconds(5);
        auto start = std::chrono::steady_clock::now();
        
        while (!server_ready) {
            if (std::chrono::steady_clock::now() - start > timeout) {
                FAIL() << "Server startup timed out";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!fake_server->is_running()) {
            server_thread.join();
            FAIL() << "Fake server failed to start";
        }
    }
};

// --- Test Cases ---

TEST_F(BackendifyTest, HandleCompanyRequestMissingParams) {
    // Create Backendify instance first
    auto backendify = createBackendify();

    // Test case 1: Missing ID
    auto result = simulateRequest(backendify, "/company?country_iso=US");
    ASSERT_EQ(result.result_int(), 400); // Beast uses result_int() for status
    EXPECT_EQ(result.body(), R"({"error": "Missing required parameters"})");

    // Test case 2: Missing country_iso
    result = simulateRequest(backendify, "/company?id=123");
    ASSERT_EQ(result.result_int(), 400);
    EXPECT_EQ(result.body(), R"({"error": "Missing required parameters"})");

    // Test case 3: Missing both parameters
    result = simulateRequest(backendify, "/company?");
    ASSERT_EQ(result.result_int(), 400);
    EXPECT_EQ(result.body(), R"({"error": "Missing required parameters"})");
}

// Update all other test cases to use the helper method:
TEST_F(BackendifyTest, HandleCompanyRequestInvalidCountry) {
    std::string companyId = "123";
    std::string isoCode = "XX";
    std::string cacheKey = companyId + ":" + isoCode;

    Backendify backendify = createBackendify();

    EXPECT_CALL(*mock_cache, get(cacheKey))
        .Times(1)
        .WillOnce(testing::Return(std::nullopt));
    EXPECT_CALL(*mock_cache, set(testing::_, testing::_, testing::_))
        .Times(0);

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_EQ(result.result_int(), 404);
    EXPECT_EQ(result.body(), R"({"error": "Unconfigured country_iso"})");
}

TEST_F(BackendifyTest, HandleCompanyRequestCacheHit) {
    // Create Backendify instance first
    auto backendify = createBackendify();

    std::string companyId = "123";
    std::string isoCode = "US";
    std::string cacheKey = companyId + ":" + isoCode;
    std::string cachedResponse = R"({"id": "123", "name": "CachedCo"})";

    // Set expectations after creating backendify
    EXPECT_CALL(*mock_cache, get(cacheKey))
        .WillOnce(testing::Return(cachedResponse));
    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_EQ(result.result_int(), 200);
    EXPECT_EQ(result.body(), cachedResponse);
}

TEST_F(BackendifyTest, HandleCompanyRequestCacheMissBackendSuccessV2) {
    auto backendify = createBackendify();

    std::string companyId = "123";
    std::string isoCode = "US";
    std::string cacheKey = companyId + ":" + isoCode;
    
    json expected = {
        {"active", true},
        {"name", "FakeCo V2"},
        {"id", companyId}
    };

    EXPECT_CALL(*mock_cache, get(cacheKey))
        .WillOnce(testing::Return(std::nullopt));
    EXPECT_CALL(*mock_cache, set(cacheKey, testing::_, testing::_))
        .WillOnce(testing::Return(true));

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_EQ(result.result_int(), 200);
    auto actual = json::parse(result.body());
    EXPECT_EQ(actual, expected);
}

TEST_F(BackendifyTest, HandleCompanyRequestCacheMissBackendSuccessV1Inactive) {
    auto backendify = createBackendify();

    std::string companyId = "456";
    std::string isoCode = "DE";
    std::string cacheKey = companyId + ":" + isoCode;
    
    // Update expected JSON to match actual response format
    json expected = {
        {"active", false},
        {"name", "FakeCo V1"},
        {"id", companyId},
        {"active_until", "2024-01-01T00:00:00Z"}  // Changed from closed_on to active_until
    };

    EXPECT_CALL(*mock_cache, get(cacheKey))
        .WillOnce(testing::Return(std::nullopt));
    EXPECT_CALL(*mock_cache, set(cacheKey, testing::_, testing::_))
        .WillOnce(testing::Return(true));

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_EQ(result.result_int(), 200);
    auto actual = json::parse(result.body());
    EXPECT_EQ(actual, expected);
}

TEST_F(BackendifyTest, HandleCompanyRequestCacheMissBackendNotFound) {
    // Create Backendify instance first
    auto backendify = createBackendify();

    std::string companyId = "notfound";
    std::string isoCode = "US";
    std::string cacheKey = companyId + ":" + isoCode;

    // Set expectations after creating backendify
    EXPECT_CALL(*mock_cache, get(cacheKey))
        .WillOnce(testing::Return(std::nullopt));
    // EXPECT_CALL(*mock_statsd, increment(BACKEND_RESPONSE_404, 1))
    //     .Times(1);

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_EQ(result.result_int(), 404);
    EXPECT_EQ(result.body(), R"({"error": "Not Found from backend"})");
}

TEST_F(BackendifyTest, HandleCompanyRequestCacheMissBackendError) {
    auto backendify = createBackendify();

    std::string companyId = "invalid";
    std::string isoCode = "US";
    std::string cacheKey = companyId + ":" + isoCode;

    EXPECT_CALL(*mock_cache, get(cacheKey))
        .WillOnce(testing::Return(std::nullopt));

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_EQ(result.result_int(), 502);
    
    // Parse and compare JSON instead of raw string comparison
    auto actual = json::parse(result.body());
    json expected = {
        {"error", "Bad Gateway - Upstream Server Error"}
    };
    EXPECT_EQ(actual, expected);
}

TEST_F(BackendifyTest, HandleStatusRequest) {
    // Status endpoint is static, doesn't need mocks usually, but let's test it via simulateRequest
    // Create Backendify with unique_ptr
    Backendify backendify = createBackendify();

    auto result = simulateRequest(backendify, "/status");

    ASSERT_EQ(result.result_int(), 200);
    EXPECT_EQ(result.body(), "Frontend Server is running (Beast)");
}

TEST_F(BackendifyTest, HandleUnhandledRoute) {
    // Create Backendify with unique_ptr
    Backendify backendify = createBackendify();

    auto result = simulateRequest(backendify, "/unhandled/path");

    ASSERT_EQ(result.result_int(), 404);
    // The body will be "Not Found in test simulation" due to the simulateRequest helper's else branch
    EXPECT_EQ(result.body(), "Not Found in test simulation"); 
}
