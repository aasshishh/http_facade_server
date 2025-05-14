#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring> 
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../config/AppConfig.hpp"

using namespace std;

class Utils {
public:
    // Converts a string to a LogLevel enum
    static LogUtils::LogLevel stringToLogLevel(const std::string& level) {
        if (level == "DEBUG") return LogUtils::LogLevel::DEBUG;
        if (level == "INFO") return LogUtils::LogLevel::INFO;
        if (level == "WARNING") return LogUtils::LogLevel::WARN;
        if (level == "CERROR") return LogUtils::LogLevel::CERROR;
        throw std::invalid_argument("Invalid log level: " + level);
    }

    // Helper to parse integer safely
    static optional<int> stringToInt(const std::string& str) {
        try {
            size_t pos;
            int val = std::stoi(str, &pos);
            // Check if the entire string was consumed
            if (pos == str.length()) {
                return val;
            }
        } catch (const std::invalid_argument&) {
            // Not an integer
        } catch (const std::out_of_range&) {
            // Integer out of range
        }
        return std::nullopt;
    }

    // Helper to trim whitespace from start and end of string
    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (string::npos == first) return str;
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }

    // Function to parse key-value pairs from a string (using optional version)
    static optional<map<string, string>> parseArguments(const vector<string>& args) {
        map<string, string> argMap;
        for (const string& arg : args) {
            size_t delimiterPos = arg.find('=');
            if (delimiterPos != string::npos && delimiterPos > 0) { // Ensure key is not empty
                string key = arg.substr(0, delimiterPos);
                string value = arg.substr(delimiterPos + 1);
                argMap[key] = value;
            } else {
                cerr << "Error: Invalid argument format: '" << arg << "'. Expected non-empty key=value format." << endl;
                return nullopt; // Signal failure
            }
        }
        return argMap; // Signal success
    }

    // Load configuration from command-line arguments
    static AppConfig loadConfiguration(const map<string, string>& startupArguments) {
        AppConfig config;

        // --- Load from Config File ---
        // Try multiple config file locations
        std::vector<std::string> config_paths = {
            "backendify.config",              // Current directory
            "../backendify.config",           // Parent directory
            "/app/backendify.config",         // Docker container path
            "../../backendify.config"         // Development path
        };

        bool config_found = false;
        for (const auto& config_path : config_paths) {
            std::ifstream configFile(config_path);
            if (configFile.is_open()) {
                cout << "Reading configuration from " << config_path << "..." << endl;
                config_found = true;
                std::string line;
                while (getline(configFile, line)) {
                    line = trim(line);
                    if (line.empty() || line[0] == '#') { // Skip empty lines and comments
                        continue;
                    }
                    size_t delimiterPos = line.find('=');
                    if (delimiterPos != string::npos && delimiterPos > 0) {
                        string key = trim(line.substr(0, delimiterPos));
                        string value = trim(line.substr(delimiterPos + 1));

                        // Apply config file values
                        if (key == "frontend_port") {
                            if (auto val = stringToInt(value)) {
                                config.frontend_port = *val;
                            } else {
                                cerr << "Warning: Invalid integer for frontend_port in config file: " << value << endl;
                            }
                        } else if (key == "redis_host") {
                            config.redis_host = value;
                        } else if (key == "redis_port") {
                            if (auto val = stringToInt(value)) {
                                config.redis_port = *val;
                            } else {
                                cerr << "Warning: Invalid integer for redis_port in config file: " << value << endl;
                            }
                        } else if (key == "log_level") {
                            config.log_level = stringToLogLevel(value);
                        } else if (key == "redis_ttl") {
                            if (auto val = stringToInt(value)) {
                                // value provided in hours
                                config.redis_ttl = (*val) * 3600;
                            } else {
                                cerr << "Warning: Invalid integer for redis_ttl in config file: " << value << endl;
                            } 
                        } else if (key == "metrics_batch_size") {
                            if (auto val = stringToInt(value)) {
                                config.metrics_batch_size = (*val);
                            } else {
                                cerr << "Warning: Invalid integer for metrics_batch_size in config file: " << value << endl;
                            } 
                        } else if (key == "metrics_send_interval") {
                            if (auto val = stringToInt(value)) {
                                // value provided in millis
                                config.metrics_send_interval_in_millis = (*val);
                            } else {
                                cerr << "Warning: Invalid integer for metrics_send_interval_in_millis in config file: " << value << endl;
                            } 
                        } else if (key == "in_memory_cache_max_size") {
                            if (auto val = stringToInt(value)) {
                                config.in_memory_cache_max_size = (*val);
                            } else {
                                cerr << "Warning: Invalid integer for in_memory_cache_max_size in config file: " << value << endl;
                            } 
                        } else if (key == "in_memory_cache_ttl") {
                            if (auto val = stringToInt(value)) {
                                // value provided in hours
                                config.in_memory_cache_ttl = (*val) * 3600;
                            } else {
                                cerr << "Warning: Invalid integer for in_memory_cache_ttl in config file: " << value << endl;
                            } 
                        } else if (key == "use_redis") {
                            if (auto val = stringToInt(value)) {
                                config.use_redis = (val == 1);
                            } else {
                                cerr << "Warning: Invalid integer for use_redis in config file: " << value << endl;
                            } 
                        } 
                    }
                }
                break;
            }
        }

        if (!config_found) {
            cerr << "Warning: Configuration file not found in any standard location. Using defaults and command-line arguments." << endl;
        }

        // Process StartUp Arguments
        // --- Populate Country to Backend URL Map ---
        // New Format: XX=http://host:port (e.g., US=http://us-backend:9005)
        cout << "Processing backend mappings..." << endl;
        for(const auto& pair : startupArguments) {
            const string& key = pair.first;
            const string& value = pair.second;

            // Check if the key looks like a 2-letter country code
            if (key.length() == 2 &&
                all_of(key.begin(), key.end(), ::isalpha)) {
                // Convert key to uppercase for consistency
                string country_iso = key;
                transform(country_iso.begin(), country_iso.end(), country_iso.begin(), ::toupper);

                // Basic validation for the URL format (parseUrl will do more detailed check later)
                if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) {
                    BackendUrlInfo urlInfo;
                    urlInfo.url = value;
                    if (Utils::parseUrl(value, &urlInfo)) {
                        config.country_backend_map[country_iso] = std::move(urlInfo);
                    } else {
                        std::cerr << "Error: Invalid backend URL format provided to callBackendApi: " << value << std::endl;    
                    }
                } else {
                    cerr << "Warning: Invalid URL format provided for country '" << country_iso << "': '" << value << "'. Expected URL starting with http:// or https://." << endl;
                }
            }
        }
        // --- End Map Population ---

        return config;
    }

    // Helper function to convert a UTC std::tm struct to time_t (seconds since epoch)
    // Uses non-standard but common functions.
    // Returns -1 on error (and sets errno).
    static time_t timegm_custom(std::tm* tm) {
        time_t result;
        // Make a copy because timegm/mkgmtime might modify the input struct
        std::tm tm_copy = *tm; 
    #if defined(_WIN32) || defined(_WIN64)
        result = _mkgmtime(&tm_copy);
        if (result == -1) {
            // _mkgmtime sets errno on overflow/invalid date
            // No explicit EOVERFLOW on Windows docs, but worth checking common patterns
            if (errno == EINVAL) { 
                std::cerr << "Warning: _mkgmtime failed, invalid date components.\n";
            }
        }
    #elif defined(__unix__) || defined(__APPLE__)
        result = timegm(&tm_copy);
        if (result == (time_t)-1) {
            // timegm might set errno, e.g., EOVERFLOW if date is out of range for time_t
            if (errno == EOVERFLOW) {
                std::cerr << "Warning: timegm failed, date out of range for time_t.\n";
            } else if (errno == EINVAL) {
                std::cerr << "Warning: timegm failed, invalid date components.\n";
            } else {
                std::cerr << "Warning: timegm failed with errno " << errno << "\n";
            }
        }
    #else
        #warning "timegm/_mkgmtime not available for this platform. UTC conversion may be incorrect or fail."
        // Fallback: Try mktime, assuming system timezone *might* be UTC (UNSAFE assumption)
        // Or, preferably, signal an error or use a C++20 chrono approach if possible.
        errno = 0; // Clear errno before potentially unsafe call
        result = std::mktime(&tm_copy); // THIS ASSUMES LOCAL TIME == UTC!
        if (result == -1) {
        std::cerr << "Warning: mktime fallback failed.\n";
        } else {
        std::cerr << "Warning: Using mktime as fallback - result depends on system timezone being UTC.\n";
        }
    #endif
        return result;
    }

    // Function to compare an RFC 3339 UTC timestamp string with the current time
    // Returns:
    //  - true if active_until is in the future
    //  - false if active_until is in the past or now
    // Throws:
    //  - std::runtime_error on parsing or conversion failure
    static bool isUTCTimeInFuture(const std::string& active_until_str) {
        
        std::tm tm = {}; // Zero-initialize struct tm
        // Get the current time
        const auto now_tp = std::chrono::system_clock::now();
        std::istringstream ss(active_until_str);

        // Parse the main date and time components
        ss >> std::get_time(&tm, Constants::TIME_FORMAT);

        if (ss.fail()) {
            throw std::runtime_error("Failed to parse date/time part: '" + active_until_str + "'");
        }

        // Handle potential fractional seconds (optional but good practice)
        // RFC 3339 allows e.g., ".123". We'll consume but ignore them for comparison precision.
        if (ss.peek() == '.') {
            char decimal_point;
            ss >> decimal_point; // Consume '.'
            int digit;
            while (ss.peek() >= '0' && ss.peek() <= '9') {
                ss >> digit; // Consume digits
            }
            if(ss.fail() && !ss.eof()) { // Check if reading digits failed unexpectedly
                ss.clear(); // Clear fail state to check the next char
            }
        }


        // Check for the trailing 'Z' (indicating UTC)
        char suffix;
        if (!(ss >> suffix) || suffix != 'Z') {
            // Check if we already reached end-of-string *exactly* after parsing seconds (no Z)
            if (ss.eof() && suffix != 'Z' && ss.fail()) {
                throw std::runtime_error("Timestamp missing required 'Z' (UTC indicator): '" + active_until_str + "'");
            }
            // Check for other unexpected characters
            if (suffix != 'Z') {
                throw std::runtime_error("Unsupported timestamp format or extra characters (expected 'Z'): '" + active_until_str + "'");
            }
            // If reading the suffix failed but it wasn't EOF, there's another issue
            if (ss.fail() && !ss.eof()) {
                throw std::runtime_error("Failed reading character after time (expected 'Z'): '" + active_until_str + "'");
            }
        }

        // Check if there's anything left in the stringstream after 'Z'
        std::string remaining;
        if (ss >> remaining && !remaining.empty()) {
            throw std::runtime_error("Unexpected characters after 'Z': '" + remaining + "' in '" + active_until_str + "'");
        }


        // Convert the parsed std::tm (which is in UTC) to a time_t
        errno = 0; // Reset errno before calling timegm_custom
        time_t active_until_time_t = Utils::timegm_custom(&tm);

        if (active_until_time_t == (time_t)-1 && errno != 0) {
            // Check if timegm_custom failed (e.g., date out of range for time_t)
            // throw std::runtime_error("Failed to convert parsed UTC time to time_t for: '" + active_until_str + "'. Errno: " + std::to_string(errno));
            return false;
        }
        if (active_until_time_t == (time_t)-1 && errno == 0) {
            // timegm_custom might return -1 without setting errno if the tm struct was invalid
            // This path might also be taken if the non-standard function isn't available and returned -1
            throw std::runtime_error("Failed to convert parsed UTC time to time_t (invalid input or missing timegm/mkgmtime?) for: '" + active_until_str + "'");
        }

        // Convert time_t to std::chrono::system_clock::time_point
        auto active_until_tp = std::chrono::system_clock::from_time_t(active_until_time_t);

        return active_until_tp > now_tp;
    }

    static bool parseUrl(const std::string& url, BackendUrlInfo* urlInfo) {
        string host;
        int port;
        bool is_https = false;
        std::smatch match;
    
        if (std::regex_match(url, match, Constants::url_regex)) {
            std::string scheme = match[1].str();
            is_https = (scheme == "https");
            host = match[2].str();
    
            if (match[3].matched) { // Port is specified
                try {
                    port = std::stoi(match[3].str());
                    if (port <= 0 || port > 65535) {
                        std::cerr << "Warning: Invalid port number " << port << " in URL " << url << std::endl;
                        return false; // Invalid port range
                    }
                } catch (const std::invalid_argument& e) {
                     std::cerr << "Error parsing port in URL " << url << ": " << e.what() << std::endl;
                    return false; // Failed to parse port
                } catch (const std::out_of_range& e) {
                     std::cerr << "Error parsing port in URL " << url << ": " << e.what() << std::endl;
                    return false; // Port out of range
                }
            } else {
                // Default port based on scheme
                port = is_https ? 443 : 80;
            }
            urlInfo->backend_host = host;
            urlInfo->backend_port = port;
            urlInfo->is_https = is_https;
            return true;
        }
        std::cerr << "Error: URL format does not match expected pattern: " << url << std::endl;
        return false; // URL format doesn't match
    }
};

#endif // UTILS_HPP