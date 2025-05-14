#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "httplib.h"

// using namespace httplib;
// using namespace std;

// Function to parse key-value pairs from a string
// Returns std::nullopt if any argument is invalid
std::optional<std::map<std::string, std::string>> parseArguments(const std::vector<std::string>& args) {
    std::map<std::string, std::string> argMap;
    for (const std::string& arg : args) {
        size_t delimiterPos = arg.find('=');
        if (delimiterPos != std::string::npos && delimiterPos > 0) { // Ensure key is not empty
            std::string key = arg.substr(0, delimiterPos);
            std::string value = arg.substr(delimiterPos + 1);
            argMap[key] = value;
        } else {
            std::cerr << "Error: Invalid argument format: '" << arg << "'. Expected non-empty key=value format." << std::endl;
            return std::nullopt; // Signal failure
        }
    }
    return argMap; // Signal success
}


int main(int argc, char** argv) {
    // 1. Process command-line arguments.
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) { // Start from 1 to skip the program name.
        args.push_back(argv[i]);
    }

    // Parse the arguments into a map.
    std::optional<std::map<std::string, std::string>> parsedArgsOpt = parseArguments(args);

    // Check if parsing was successful
    if (!parsedArgsOpt) {
        std::cerr << "Failed to parse command-line arguments. Exiting." << std::endl;
        return 1; // Exit if arguments are invalid
    }

    // Get the successfully parsed arguments
    std::map<std::string, std::string> startupArguments = *parsedArgsOpt; // Dereference optional

    // Print the parsed arguments.
    std::cout << "Startup Arguments:" << std::endl;
    for (const auto& pair : startupArguments) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
    }

    // --- Configuration from Arguments (with defaults) ---
    int port = 9001; // Default port
    std::string defaultCompanyName = "DefaultCompany Inc."; // Default company name for /company route
    std::string statusMessage = "Server is running (Default)"; // Default status

    if (startupArguments.count("port")) {
        try {
            port = std::stoi(startupArguments["port"]);
            if (port <= 0 || port > 65535) {
                 std::cerr << "Warning: Invalid port number '" << startupArguments["port"] << "'. Using default port " << 9001 << "." << std::endl;
                 port = 9001;
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "Warning: Invalid port number format '" << startupArguments["port"] << "'. Using default port " << 9001 << "." << std::endl;
            port = 9001;
        } catch (const std::out_of_range& e) {
             std::cerr << "Warning: Port number '" << startupArguments["port"] << "' out of range. Using default port " << 9001 << "." << std::endl;
             port = 9001;
        }
    }

    if (startupArguments.count("company_name")) {
        defaultCompanyName = startupArguments["company_name"];
    }
     if (startupArguments.count("status_message")) {
        statusMessage = startupArguments["status_message"];
    }
    // --- End Configuration ---


    httplib::Server server;

    // Handle GET requests for /status.
    server.Get("/status", [statusMessage](const httplib::Request& req, httplib::Response& res) {
        std::cout << "Received request for /status" << std::endl;
        res.set_content(statusMessage, "text/plain");
    });

    // Handle GET requests for /company
    server.Get("/company", [defaultCompanyName](const httplib::Request& req, httplib::Response& res) {
        std::cout << "Received request for /company" << std::endl;
        res.set_content(defaultCompanyName, "text/plain");
    });

    // Handle GET requests for /company/<id>
    server.Get(R"(/companies/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string company_id = req.matches[1].str(); // Use .str() to get std::string

        std::cout << "Received request for /companies/" << company_id << std::endl;

        std::string json_response;
        if (company_id == "12345") {
            json_response = R"({"cn": "ABCDE", "created_on" :"1786-06-30T06:23:14Z"})"; // Use raw string literal for JSON
            // Set the response content type to application/x-company-v2
            res.set_content(json_response, "application/x-company-v1");
            res.status = 200; // OK
        } else if (company_id == "123456") {
            json_response = R"({"cn": "ABCDEF", "created_on" :"1786-06-30T06:23:14Z", "closed_on" :"2002-06-30T06:23:14Z"})"; // Use raw string literal for JSON
            // Set the response content type to application/x-company-v2
            res.set_content(json_response, "application/x-company-v1");
            res.status = 200; // OK
        } else if (company_id == "1234567") {
            json_response = R"({"cn": "ABCDEFG", "created_on" :"1786-06-30T06:23:14Z", "closed_on" :"2050-06-30T06:23:14Z"})"; // Use raw string literal for JSON
            // Set the response content type to application/x-company-v2
            res.set_content(json_response, "application/x-company-v1");
            res.status = 200; // OK
        } else if (company_id == "2345") {
            json_response = R"({"company_name": "BCDE", "tin" :"1786-06-30T06:23:14Z"})"; // Use raw string literal for JSON
            // Set the response content type to application/x-company-v2
            res.set_content(json_response, "application/x-company-v2");
            res.status = 200; // OK
        } else if (company_id == "23456") {
            json_response = R"({"company_name": "BCDEF", "tin" :"1786-06-30T06:23:14Z", "dissolved_on" :"2002-06-30T06:23:14Z"})"; // Use raw string literal for JSON
            // Set the response content type to application/x-company-v2
            res.set_content(json_response, "application/x-company-v2");
            res.status = 200; // OK
        } else if (company_id == "234567") {
            json_response = R"({"company_name": "BCDEFG", "tin" :"1786-06-30T06:23:14Z", "dissolved_on" :"1786-08-30T06:23:14Z"})"; // Use raw string literal for JSON
            // Set the response content type to application/x-company-v2
            res.set_content(json_response, "application/x-company-v2");
            res.status = 200; // OK
        } else {
            // ID not found
            json_response = R"({"status": "not_found"})"; // Use raw string literal for JSON
            // Set the response content type to application/json
            res.set_content(json_response, "application/json");
            res.status = 404; // Not Found
        }
    });

    // Handle all other GET requests with a 404 Not Found.
    server.Get(".*", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "Received unhandled GET request for " << req.path << std::endl;
        res.status = 404; // Set HTTP status code to 404
        res.set_content("Not Found", "text/plain");
    });

    // Start the server and print a message to the console.
    std::cout << "Starting server on 0.0.0.0:" << port << "..." << std::endl;
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to start server on port " << port << "." << std::endl;
        return 1;
    }

    std::cout << "Server stopped." << std::endl;
    return 0;
}
