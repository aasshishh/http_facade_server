#ifndef INMEMORYCACHE_HPP
#define INMEMORYCACHE_HPP

#include <iostream> // For potential debug logging
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "../interfaces/CacheInterface.hpp"

struct CacheEntry {
    std::string value; // The actual data
    std::chrono::time_point<std::chrono::steady_clock> expiry; // Use steady_clock for TTL
};

class InMemoryCache : public CacheInterface {
private:
    std::unordered_map<std::string, CacheEntry> cache_; // Stores key -> {value, expiry}
    std::list<std::string> lru_list_;                   // Stores keys in LRU order (front=most recent, back=least recent)
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_; // Maps key -> iterator in lru_list_

    mutable std::mutex mutex_;
    const int default_ttl_seconds_;
    const size_t max_size_;

    void removeExpired(); // Renamed from cleanup for clarity
    void evictIfNeeded(); // Helper to enforce size limit

public:
    // Constructor with default TTL and max size
    explicit InMemoryCache(int default_ttl_seconds = 3600 * 24, size_t max_size = 10000);

    ~InMemoryCache() override = default;

    bool set(const std::string& key, const std::string& value, int ttl = 0) override;
    std::optional<std::string> get(const std::string& key) override;
    bool remove(const std::string& key) override;
    bool clear() override;
    bool exists(const std::string& key) override;
};

#endif // INMEMORYCACHE_HPP
