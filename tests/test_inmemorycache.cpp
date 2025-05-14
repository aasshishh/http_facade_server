// test/test_inmemorycache.cpp
#include <chrono> // For std::chrono::seconds
#include <string>
#include <thread> // For std::this_thread::sleep_for

#include "gtest/gtest.h"

#include "../src/cache/InMemoryCache.hpp" // Include the header

TEST(InMemoryCacheTest, SetAndGet) {
    InMemoryCache cache;
    std::string key = "123:US";
    std::string value = R"({"name":"TestCo"})";

    EXPECT_TRUE(cache.set(key, value));
    auto retrievedData = cache.get(key);
    ASSERT_TRUE(retrievedData.has_value());
    EXPECT_EQ(*retrievedData, value);
}

TEST(InMemoryCacheTest, GetNonExistent) {
    InMemoryCache cache;
    std::string key = "999:GB";

    auto retrievedData = cache.get(key);
    EXPECT_FALSE(retrievedData.has_value());
}

TEST(InMemoryCacheTest, GetExpired) {
    InMemoryCache cache;
    std::string key = "456:DE";
    std::string value = R"({"name":"ExpiredCo"})";

    EXPECT_TRUE(cache.set(key, value, 1)); // 1 second TTL

    // Wait for longer than the TTL
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto retrievedData = cache.get(key);
    EXPECT_FALSE(retrievedData.has_value());
}

TEST(InMemoryCacheTest, GetNotExpired) {
    InMemoryCache cache;
    std::string key = "789:FR";
    std::string value = R"({"name":"ValidCo"})";

    EXPECT_TRUE(cache.set(key, value, 3600)); // 1 hour TTL

    // Wait for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto retrievedData = cache.get(key);
    ASSERT_TRUE(retrievedData.has_value());
    EXPECT_EQ(*retrievedData, value);
}

TEST(InMemoryCacheTest, OverwriteEntry) {
    InMemoryCache cache;
    std::string key = "111:JP";
    std::string value1 = R"({"name":"OldCo"})";
    std::string value2 = R"({"name":"NewCo"})";

    EXPECT_TRUE(cache.set(key, value1));
    auto retrieved1 = cache.get(key);
    ASSERT_TRUE(retrieved1.has_value());
    EXPECT_EQ(*retrieved1, value1);

    EXPECT_TRUE(cache.set(key, value2)); // Overwrite
    auto retrieved2 = cache.get(key);
    ASSERT_TRUE(retrieved2.has_value());
    EXPECT_EQ(*retrieved2, value2);
}

TEST(InMemoryCacheTest, RemoveEntry) {
    InMemoryCache cache;
    std::string key = "222:CN";
    std::string value = R"({"name":"ToBeRemoved"})";

    EXPECT_TRUE(cache.set(key, value));
    EXPECT_TRUE(cache.exists(key));
    EXPECT_TRUE(cache.remove(key));
    EXPECT_FALSE(cache.exists(key));
}

TEST(InMemoryCacheTest, ClearCache) {
    InMemoryCache cache;
    std::string key1 = "333:IN";
    std::string key2 = "444:BR";
    std::string value = R"({"name":"ToBeClearedCo"})";

    EXPECT_TRUE(cache.set(key1, value));
    EXPECT_TRUE(cache.set(key2, value));
    EXPECT_TRUE(cache.exists(key1));
    EXPECT_TRUE(cache.exists(key2));
    EXPECT_TRUE(cache.clear());
    EXPECT_FALSE(cache.exists(key1));
    EXPECT_FALSE(cache.exists(key2));
}
