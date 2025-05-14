#include <iostream>
#include <chrono>
#include <thread>

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

#include "RedisCache.hpp"
#include "../config/AppConfig.hpp"
#include "../interfaces/ILogger.hpp"

using json = nlohmann::json;

RedisCache::RedisCache(const AppConfig& config, std::shared_ptr<ILogger> logger)
    : config_(config), logger_(logger), redis_context_(nullptr) {
    connect();
}

RedisCache::~RedisCache() {
    if (redis_context_) {
        redisFree(redis_context_);
    }
}

void RedisCache::connect() {
    redis_context_ = redisConnect(config_.redis_host.c_str(), config_.redis_port);
    if (redis_context_ == nullptr || redis_context_->err) {
        std::string error_msg;
        if (redis_context_) {
            error_msg = "Redis connection error: " + std::string(redis_context_->errstr);
            redisFree(redis_context_);
            redis_context_ = nullptr;
        } else {
            error_msg = "Redis connection error: can't allocate redis context";
        }
        logger_->error(error_msg);
    }
}

bool RedisCache::set(const std::string& key, const std::string& value, int ttl) {
        std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety
        if (!redis_context_) {
            logger_->error("Redis not connected. Cannot SET key: " + key);
            return false;
        }

        redisReply* reply;
        if (ttl > 0) {
            reply = (redisReply*)redisCommand(redis_context_, 
                "SETEX %s %d %s", 
                key.c_str(), 
                ttl, 
                value.c_str());
        } else {
            reply = (redisReply*)redisCommand(redis_context_, 
                "SET %s %s", 
                key.c_str(), 
                value.c_str());
        }

        if (reply == nullptr) {
            logger_->error("Redis SET/SETEX command failed (nullptr reply) for key: " + key);
            return false;
        }

        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply);
        return success;
}

void RedisCache::set(const std::string& key, const json& value) {
    try {
        // Use default TTL from config if not specified otherwise
        if (!set(key, value.dump(), config_.redis_ttl)) { 
            logger_->error("Failed to set JSON value in Redis for key: " + key);
        }
    } catch (const std::exception& e) {
        logger_->error("Exception during JSON set for key '" + key + "': " + e.what());
    }
}

std::optional<std::string> RedisCache::get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety
        if (!redis_context_) {
            logger_->error("Redis not connected. Cannot GET key: " + key);
            return std::nullopt;
        }

        redisReply* reply = (redisReply*)redisCommand(redis_context_, "GET %s", key.c_str());
        if (reply == nullptr) {
            return std::nullopt;
        }

        std::optional<std::string> result;
        if (reply->type == REDIS_REPLY_STRING) {
            result = std::string(reply->str, reply->len);
        }

        freeReplyObject(reply);
        return result;
}

std::optional<json> RedisCache::get_json(const std::string& key) {
    auto str_value = get(key);
    if (!str_value) {
        return std::nullopt;
    }

    try {
        return json::parse(*str_value);
    } catch (const json::parse_error& e) {
        logger_->error("JSON parse error for key '" + key + "': " + e.what());
        return std::nullopt;
    }
}

bool RedisCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety
    if (!redis_context_) {
        logger_->error("Redis not connected. Cannot DEL key: " + key);
        return false;
    }
    redisReply* reply = (redisReply*)redisCommand(redis_context_, "DEL %s", key.c_str());
    
    if (reply == nullptr) {
        return false;
    }

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return success;
}

bool RedisCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety
    if (!redis_context_) {
        logger_->error("Redis not connected. Cannot FLUSHALL.");
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(redis_context_, "FLUSHALL");
    if (reply == nullptr) {
        return false;
    }

    bool success = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    return success;
}

bool RedisCache::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!redis_context_) {
        logger_->error("Redis not connected. Cannot check EXISTS for key: " + key);
        return false;
    }
    redisReply* reply = (redisReply*)redisCommand(redis_context_, "EXISTS %s", key.c_str());
    if (reply == nullptr) {
        return false;
    }

    bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return exists;
}

bool RedisCache::isConnected() const {
    return redis_context_ != nullptr;
}