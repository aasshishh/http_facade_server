#pragma once

#include <iostream>
#include <memory>
#include <mutex>

#include "../config/AppConfig.hpp"
#include "../interfaces/ILogger.hpp"

class ConsoleLogger : public ILogger {
public:
    static std::shared_ptr<ConsoleLogger> getInstance(LogUtils::LogLevel logLevel);
    ~ConsoleLogger() override = default;

    void info(const std::string& message) override;    
    void debug(const std::string& message) override;
    void warn(const std::string& message) override;
    void error(const std::string& message) override;
    void setup(const std::string& message) override;
    int getLogLevel() { return logLevel; }

private:
    explicit ConsoleLogger(LogUtils::LogLevel logLevel) : logLevel(logLevel) {}
    LogUtils::LogLevel logLevel;
    std::mutex cout_mutex_; // Mutex to protect std::cout access

    static std::shared_ptr<ConsoleLogger> instance;
    static std::once_flag init_flag;

    // Delete copy/move operations
    ConsoleLogger(const ConsoleLogger&) = delete;
    ConsoleLogger& operator=(const ConsoleLogger&) = delete;
    ConsoleLogger(ConsoleLogger&&) = delete;
    ConsoleLogger& operator=(ConsoleLogger&&) = delete;
};