// test/test_utils.cpp
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "../src/utils/Utils.hpp" // Include the header with the class definition
#include "../src/models/BackendUrlInfo.hpp"

// Add this near the top of the file, after includes
namespace testing {
    void PrintTo(const BackendUrlInfo& info, std::ostream* os) {
        *os << "BackendUrlInfo{"
            << "url: " << info.url
            << ", backend_host: " << info.backend_host
            << ", backend_port: " << info.backend_port
            << ", is_https: " << (info.is_https ? "true" : "false")
            << "}";
    }
}

// --- Tests for parseArguments ---

TEST(UtilsTest, ParseArgumentsValidSingle) {
    std::vector<std::string> args = {"key=value"};
    auto result = Utils::parseArguments(args);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ(result->at("key"), "value");
}

TEST(UtilsTest, ParseArgumentsValidMultiple) {
    std::vector<std::string> args = {"key1=value1", "key2=value2"};
    auto result = Utils::parseArguments(args);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);
    EXPECT_EQ(result->at("key1"), "value1");
    EXPECT_EQ(result->at("key2"), "value2");
}

TEST(UtilsTest, ParseArgumentsEmptyInput) {
    std::vector<std::string> args = {};
    auto result = Utils::parseArguments(args);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(UtilsTest, ParseArgumentsInvalidNoEquals) {
    std::vector<std::string> args = {"keyvalue"};
    auto result = Utils::parseArguments(args);
    EXPECT_FALSE(result.has_value());
}

TEST(UtilsTest, ParseArgumentsInvalidEmptyKey) {
    std::vector<std::string> args = {"=value"};
    auto result = Utils::parseArguments(args);
    EXPECT_FALSE(result.has_value());
}

TEST(UtilsTest, ParseArgumentsEmptyValue) {
    std::vector<std::string> args = {"key="};
    auto result = Utils::parseArguments(args);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ(result->at("key"), "");
}

TEST(UtilsTest, ParseArgumentsMixedValidInvalid) {
    // The current implementation returns nullopt if *any* arg is invalid
    std::vector<std::string> args = {"key1=value1", "invalid", "key2=value2"};
    auto result = Utils::parseArguments(args);
    EXPECT_FALSE(result.has_value());
}

// --- Tests for loadConfiguration ---

TEST(UtilsTest, LoadConfigurationDefaults) {
    std::map<std::string, std::string> args = {};
    AppConfig config = Utils::loadConfiguration(args);
    EXPECT_EQ(config.frontend_port, 9000);
    EXPECT_TRUE(config.country_backend_map.empty()); // Assuming no hardcoded defaults for now
}

TEST(UtilsTest, LoadConfigurationOverridePortInvalidFormat) {
    // Redirect cerr to avoid polluting test output (optional)
    std::streambuf* oldCerr = std::cerr.rdbuf();
    std::ostringstream newCerr;
    std::cerr.rdbuf(newCerr.rdbuf());

    std::map<std::string, std::string> args = {{"port", "abc"}};
    AppConfig config = Utils::loadConfiguration(args);
    EXPECT_EQ(config.frontend_port, 9000); // Should revert to default

    std::cerr.rdbuf(oldCerr); // Restore cerr
    // Optionally check newCerr.str() for the warning message
}

TEST(UtilsTest, LoadConfigurationOverridePortInvalidRange) {
    std::map<std::string, std::string> args = {{"port", "70000"}};
    AppConfig config = Utils::loadConfiguration(args);
    EXPECT_EQ(config.frontend_port, 9000); // Should revert to default
}

TEST(UtilsTest, LoadConfigurationAddCountryMapping) {
    std::map<std::string, std::string> args = {{"US", "http://us-backend:9001"}};
    AppConfig config = Utils::loadConfiguration(args);
    ASSERT_EQ(config.country_backend_map.size(), 1);
    // Compare the 'url' member, not the whole object
    EXPECT_EQ(config.country_backend_map.at("US").url, "http://us-backend:9001");
}

TEST(UtilsTest, LoadConfigurationAddCountryMappingLowercase) {
    std::map<std::string, std::string> args = {{"gb", "http://gb-backend:9002"}};
    AppConfig config = Utils::loadConfiguration(args);
    ASSERT_EQ(config.country_backend_map.size(), 1);
    // Key should be converted to uppercase
    EXPECT_TRUE(config.country_backend_map.count("GB"));
    EXPECT_FALSE(config.country_backend_map.count("gb"));
    // Compare the 'url' member, not the whole object
    EXPECT_EQ(config.country_backend_map.at("GB").url, "http://gb-backend:9002");
}

TEST(UtilsTest, LoadConfigurationIgnoreNonCountryArgs) {
    std::map<std::string, std::string> args = {{"some_other_arg", "value"}};
    AppConfig config = Utils::loadConfiguration(args);
    EXPECT_TRUE(config.country_backend_map.empty());
}

TEST(UtilsTest, LoadConfigurationInvalidCountryUrl) {
    std::map<std::string, std::string> args = {{"DE", "invalid-url"}};
    // Redirect cerr
    std::streambuf* oldCerr = std::cerr.rdbuf();
    std::ostringstream newCerr;
    std::cerr.rdbuf(newCerr.rdbuf());

    AppConfig config = Utils::loadConfiguration(args);
    // The current implementation warns but does NOT add the invalid URL
    EXPECT_TRUE(config.country_backend_map.empty());

    std::cerr.rdbuf(oldCerr); // Restore cerr
    EXPECT_NE(newCerr.str().find("Warning: Invalid URL format"), std::string::npos);
}

// --- Tests for isUTCTimeInFuture ---

TEST(UtilsTest, IsUTCTimeInFuture_FutureDate) {
    // Create a timestamp for tomorrow
    auto now = std::chrono::system_clock::now();
    auto tomorrow_tp = now + std::chrono::hours(24);
    std::time_t tomorrow_time_t = std::chrono::system_clock::to_time_t(tomorrow_tp);
    std::tm tm_tomorrow = *std::gmtime(&tomorrow_time_t); // Use gmtime for UTC

    std::ostringstream oss;
    oss << std::put_time(&tm_tomorrow, Constants::TIME_FORMAT) << "Z";
    std::string future_str = oss.str();

    EXPECT_TRUE(Utils::isUTCTimeInFuture(future_str));
}

TEST(UtilsTest, IsUTCTimeInFuture_PastDate) {
    // Create a timestamp for yesterday
    auto now = std::chrono::system_clock::now();
    auto yesterday_tp = now - std::chrono::hours(24);
    std::time_t yesterday_time_t = std::chrono::system_clock::to_time_t(yesterday_tp);
    std::tm tm_yesterday = *std::gmtime(&yesterday_time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm_yesterday, Constants::TIME_FORMAT) << "Z";
    std::string past_str = oss.str();

    EXPECT_FALSE(Utils::isUTCTimeInFuture(past_str));
}

TEST(UtilsTest, IsUTCTimeInFuture_VeryPastDate_ReturnsFalse) {
    // Test with a date known to be out of time_t range for some systems
    // The function currently returns false for this due to conversion failure
    EXPECT_FALSE(Utils::isUTCTimeInFuture("1786-06-30T06:23:14Z"));
}

TEST(UtilsTest, IsUTCTimeInFuture_VeryFutureDate) {
    EXPECT_TRUE(Utils::isUTCTimeInFuture("2099-12-31T23:59:59Z"));
}

TEST(UtilsTest, IsUTCTimeInFuture_InvalidFormat_NoZ) {
    EXPECT_THROW(Utils::isUTCTimeInFuture("2025-01-01T12:00:00"), std::runtime_error);
}

TEST(UtilsTest, IsUTCTimeInFuture_InvalidFormat_ExtraChars) {
    EXPECT_THROW(Utils::isUTCTimeInFuture("2025-01-01T12:00:00Z_extra"), std::runtime_error);
}

TEST(UtilsTest, IsUTCTimeInFuture_InvalidFormat_BadDate) {
    EXPECT_THROW(Utils::isUTCTimeInFuture("2025-13-01T12:00:00Z"), std::runtime_error); // Invalid month
}

TEST(UtilsTest, IsUTCTimeInFuture_InvalidFormat_BadTime) {
    EXPECT_THROW(Utils::isUTCTimeInFuture("2025-01-01T25:00:00Z"), std::runtime_error); // Invalid hour
}

TEST(UtilsTest, IsUTCTimeInFuture_WithFractionalSeconds) {
    // Create a timestamp for tomorrow, then add fractional seconds to the string
    auto now = std::chrono::system_clock::now();
    auto tomorrow_tp = now + std::chrono::hours(24);
    std::time_t tomorrow_time_t = std::chrono::system_clock::to_time_t(tomorrow_tp);
    std::tm tm_tomorrow = *std::gmtime(&tomorrow_time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm_tomorrow, Constants::TIME_FORMAT) << ".12345Z"; // Add fractional
    std::string future_str_frac = oss.str();

    EXPECT_TRUE(Utils::isUTCTimeInFuture(future_str_frac));

    // Test past date with fractional seconds
    auto yesterday_tp = now - std::chrono::hours(24);
    std::time_t yesterday_time_t = std::chrono::system_clock::to_time_t(yesterday_tp);
    std::tm tm_yesterday = *std::gmtime(&yesterday_time_t);
    std::ostringstream oss_past;
    oss_past << std::put_time(&tm_yesterday, Constants::TIME_FORMAT) << ".999Z";
    std::string past_str_frac = oss_past.str();
    EXPECT_FALSE(Utils::isUTCTimeInFuture(past_str_frac));
}
