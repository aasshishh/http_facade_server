
#include <iostream>
#include <mutex>
#include <string>

#include "ConsoleLogger.hpp"

// Define static members
std::shared_ptr<ConsoleLogger> ConsoleLogger::instance = nullptr;
std::once_flag ConsoleLogger::init_flag;

std::shared_ptr<ConsoleLogger> ConsoleLogger::getInstance(LogUtils::LogLevel logLevel) {
    std::call_once(init_flag, [logLevel]() {
        instance.reset(new ConsoleLogger(logLevel));
    });
    return std::shared_ptr<ConsoleLogger>(instance.get());
}


void ConsoleLogger::info(const std::string& message) {
    if (logLevel <= LogUtils::LogLevel::INFO) {
        std::lock_guard<std::mutex> lock(cout_mutex_); // Lock before accessing cout
        std::cout << LogUtils::INFO_LOG_PREFIX << message << std::endl;
    }
}   

void ConsoleLogger::debug(const std::string& message) {
    if (logLevel <= LogUtils::LogLevel::DEBUG) {
        std::lock_guard<std::mutex> lock(cout_mutex_); // Lock before accessing cout
        std::cout << LogUtils::DEBUG_LOG_PREFIX << message << std::endl;
    }
}

void ConsoleLogger::warn(const std::string& message) {
    if (logLevel <= LogUtils::LogLevel::WARN) {
        std::lock_guard<std::mutex> lock(cout_mutex_); // Lock before accessing cout
        std::cout << LogUtils::WARN_LOG_PREFIX << message << std::endl;
    }
}

void ConsoleLogger::error(const std::string& message) {
    if (logLevel <= LogUtils::LogLevel::CERROR) {
        // Use std::cerr for errors
        std::lock_guard<std::mutex> lock(cout_mutex_); // Lock before accessing cerr
        std::cerr << LogUtils::CERROR_LOG_PREFIX << message << std::endl;
    }
}

void ConsoleLogger::setup(const std::string& message) {
    std::lock_guard<std::mutex> lock(cout_mutex_); // Lock before accessing cerr
    std::cout << LogUtils::SETUP_LOG_PREFIX << message << std::endl;
}