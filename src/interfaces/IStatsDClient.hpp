#pragma once

#include <chrono>
#include <string>

class IStatsDClient {
public:
    virtual ~IStatsDClient() = default;

    virtual void increment(const std::string& key, int value = 1) = 0;
    virtual void decrement(const std::string& key, int value = 1) = 0;
    virtual void gauge(const std::string& key, double value) = 0;
    virtual void timing(const std::string& key, std::chrono::milliseconds value) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;
};