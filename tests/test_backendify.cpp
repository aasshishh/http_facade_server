// test/test_backendify.cpp
#include <atomic>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>

#include "gtest/gtest.h"
#include "gmock/gmock.h"

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
    AppConfig config;
    std::atomic<bool> server_ready{false};
    int fake_server_port = 9001; // Default fake backend port

    void SetUp() override {
        updateBackendUrls();

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

    // Helper to simulate a client request to Backendify's server
    httplib::Result simulateRequest(Backendify& backendify, const std::string& path) {
        auto test_server = std::make_unique<httplib::Server>();
        backendify.setupServer(*test_server);

        // Need to run this server temporarily to handle one request
        int temp_port = 10000 + (rand() % 10000); // Find an available port
        while (!test_server->bind_to_port("127.0.0.1", temp_port)) {
             temp_port++;
             if (temp_port > 30000) throw std::runtime_error("Could not find port for simulateRequest");
        }

        std::promise<void> server_started;
        std::thread temp_server_thread([&]() {
            server_started.set_value();
            test_server->listen_after_bind();
        });

        server_started.get_future().wait(); // Wait until server is bound

        httplib::Client test_client("127.0.0.1", temp_port);
        test_client.set_connection_timeout(1);
        test_client.set_read_timeout(1);
        auto res = test_client.Get(path.c_str());

        // Ensure cleanup
        if (temp_server_thread.joinable()) {
            test_server->stop();
            temp_server_thread.join();
        }

        return res;
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
                    updateBackendUrls();
                    server_ready = true;
                    fake_server->listen_after_bind();
                    return;
                }
            }
            std::cerr << "Failed to bind fake server to any port" << std::endl;
            server_ready = true;
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
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 400);
    EXPECT_EQ(result->body, R"({"error": "Missing required parameters"})");

    // Test case 2: Missing country_iso
    result = simulateRequest(backendify, "/company?id=123");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 400);
    EXPECT_EQ(result->body, R"({"error": "Missing required parameters"})");

    // Test case 3: Missing both parameters
    result = simulateRequest(backendify, "/company?");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 400);
    EXPECT_EQ(result->body, R"({"error": "Missing required parameters"})");
}

// Update all other test cases to use the helper method:
TEST_F(BackendifyTest, HandleCompanyRequestInvalidCountry) {
    std::string companyId = "123";
    std::string isoCode = "XX";
    std::string cacheKey = companyId + ":" + isoCode;

    // Create Backendify with unique_ptr
    Backendify backendify = createBackendify();

    EXPECT_CALL(*mock_cache, get(cacheKey))
        .Times(1)
        .WillOnce(testing::Return(std::nullopt));
    EXPECT_CALL(*mock_cache, set(testing::_, testing::_, testing::_))
        .Times(0);

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 404);
    EXPECT_EQ(result->body, R"({"error": "Unconfigured country_iso"})");
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

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->body, cachedResponse);
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

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    
    auto actual = json::parse(result->body);
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

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    
    auto actual = json::parse(result->body);
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

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 404);
    EXPECT_EQ(result->body, R"({"error": "Not Found"})");
}

TEST_F(BackendifyTest, HandleCompanyRequestCacheMissBackendError) {
    auto backendify = createBackendify();

    std::string companyId = "invalid";
    std::string isoCode = "US";
    std::string cacheKey = companyId + ":" + isoCode;

    EXPECT_CALL(*mock_cache, get(cacheKey))
        .WillOnce(testing::Return(std::nullopt));

    auto result = simulateRequest(backendify, "/company?id=" + companyId + "&country_iso=" + isoCode);

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 504);
    
    // Parse and compare JSON instead of raw string comparison
    auto actual = json::parse(result->body);
    json expected = {
        {"error", "Gateway Timeout"}
    };
    EXPECT_EQ(actual, expected);
}

TEST_F(BackendifyTest, HandleStatusRequest) {
    // Status endpoint is static, doesn't need mocks usually, but let's test it via simulateRequest
    // Create Backendify with unique_ptr
    Backendify backendify = createBackendify();

    auto result = simulateRequest(backendify, "/status");

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->body, "Frontend Server is running");
}

TEST_F(BackendifyTest, HandleUnhandledRoute) {
    // Create Backendify with unique_ptr
    Backendify backendify = createBackendify();

    auto result = simulateRequest(backendify, "/unhandled/path");

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 404);
    EXPECT_EQ(result->body, "Not Found");
}
