#ifndef CACHEINTERFACE_HPP
#define CACHEINTERFACE_HPP

#include <optional>
#include <string>

class CacheInterface {
public:
    virtual ~CacheInterface() = default;
    virtual bool set(const std::string& key, const std::string& value, int ttl = 0) = 0;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual bool remove(const std::string& key) = 0;
    virtual bool clear() = 0;
    virtual bool exists(const std::string& key) = 0;
};

#endif // CACHEINTERFACE_HPP