#pragma once

#include <string>

struct BackendUrlInfo {
    std::string url;
    std::string backend_host;
    int backend_port;
    bool is_https;

    bool operator==(const BackendUrlInfo& other) const {
        return url == other.url;
    }

    // Explicitly default the copy constructor and copy assignment operator
    BackendUrlInfo(const BackendUrlInfo&) = default;
    BackendUrlInfo& operator=(const BackendUrlInfo&) = default;

    // Explicitly default the move constructor and move assignment operator
    BackendUrlInfo(BackendUrlInfo&&) = default;
    BackendUrlInfo& operator=(BackendUrlInfo&&) = default;

    BackendUrlInfo() = default; // Keep the default constructor
};