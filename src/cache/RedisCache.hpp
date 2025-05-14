#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "../config/AppConfig.hpp"
#include "../interfaces/CacheInterface.hpp"

using json = nlohmann::json;

// Forward declarations
struct redisContext;
class ILogger;

class RedisCache : public CacheInterface {
public:
    explicit RedisCache(const AppConfig& config, std::shared_ptr<ILogger> logger);
    ~RedisCache() override;

    bool set(const std::string& key, const std::string& value, int ttl = 0) override;
    std::optional<std::string> get(const std::string& key) override;
    bool remove(const std::string& key) override;
    bool clear() override;
    bool exists(const std::string& key) override;

    // Additional JSON-specific methods
    void set(const std::string& key, const json& value);
    std::optional<json> get_json(const std::string& key);

    // Check if the cache is connected to Redis
    bool isConnected() const;

private:
    void connect();
    const AppConfig& config_;
    std::shared_ptr<ILogger> logger_;
    redisContext* redis_context_;
    std::mutex mutex_;
}; 