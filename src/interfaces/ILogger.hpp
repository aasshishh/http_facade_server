#pragma once

#include <string>

class ILogger {
public:
    virtual ~ILogger() noexcept = default;
    virtual void info(const std::string& message) = 0;    
    virtual void debug(const std::string& message) = 0;
    virtual void warn(const std::string& message) = 0;
    virtual void error(const std::string& message) = 0;
    virtual void setup(const std::string& message) = 0;
    virtual int getLogLevel() = 0;
};