# Backendify: An HTTP Facade Service

Welcome to Backendify! This project aims to simplify interactions with multiple backend services by providing a single, unified API. If you're dealing with various providers for similar company data, Backendify is here to make your life easier by acting as a smart proxy.

This service is designed to be:
*   **High-performing:** Built with an asynchronous architecture using Boost.Asio and Boost.Beast to handle many concurrent requests efficiently.
*   **Resilient:** Incorporates caching and error handling strategies like retries and circuit breakers.
*   **Flexible:** Supports different backend API versions (V1 and V2) and is configurable for various deployment environments.
*   **Observable:** Includes support for metrics and structured logging.

## Core Features

*   **Unified API:** Provides a single `GET /company` endpoint to fetch company data, abstracting away backend complexities.
*   **Dynamic Backend Routing:** Routes requests to the appropriate backend service based on the `country_iso` code provided in the request.
*   **V1 & V2 Backend Support:** Seamlessly handles different JSON response formats and content types from V1 and V2 backend providers.
*   **Asynchronous Architecture:** Leverages Boost.Asio and Boost.Beast for a non-blocking, event-driven design, crucial for I/O-bound applications and high concurrency. (This is the current architecture in the `master` branch).
*   **Caching:** Implements both Redis and In-Memory caching (with LRU eviction) to improve response times and reduce load on backend services. Cache entries have a default TTL of 24 hours.
*   **Configuration:** Supports configuration via a file (`backendify\backendify.config`) and command-line arguments.
*   **Metrics & Logging:** Integrates with StatsD for metrics and provides a configurable logging framework.
*   **Dockerized:** Comes with a `backendify\Dockerfile` for easy containerization.

## Project Structure

Here's a brief overview of how the project is organized:

*   `backendify/`
    *   `backendify.config`: Runtime configuration parameters.
    *   `src/`: Contains the core source code.
        *   `main.cpp`: The application entry point.
        *   `cache/`: Caching implementations (`RedisCache`, `InMemoryCache`).
        *   `config/`: Configuration loading logic (`AppConfig`).
        *   `core/`: Main application logic (`Backendify`).
        *   `interfaces/`: Abstract interfaces (`ILogger`, `ICache`, `IStatsDClient`).
        *   `logging/`: Logging implementations (`ConsoleLogger`).
        *   `metrics/`: StatsD/metrics implementations (`StatsDClient`, `DummyStatsDClient`).
        *   `models/`: Data structures (`CompanyInfo`, `BackendUrlInfo`).
        *   `third_party/`: Headers for third-party libraries like `httplib.h` (used in synchronous version, potentially test utilities), `json.hpp`, `UDPSender.hpp`.
        *   `utils/`: Utility functions.
    *   `build/`: Build output (executables, libraries).
    *   `tests/`: Unit tests based on Google Test (`gtest`).
    *   `vcpkg/`: Dependency manager files for `vcpkg`.
    *   `BackendServers/`: A prototype backend HTTP server to support E2E testing during development.
    *   `CODE_DESIGN.md`: In-depth documentation on design decisions, architecture, and more. **This is a key document for understanding the system.**
    *   `CMakeLists.txt`: The main CMake build script.
    *   `Dockerfile`: Instructions for building the Docker image.
    *   `README.md`: This file! Project overview and instructions.

## Solution API design

The API customers are expecting is quite simple. The Application must implement just two endpoints:

1.  `GET /status`. This API endpoint must return an HTTP 200/OK when the service is ready to accept requests. Load balancers typically use this endpoint for health checks.

2. `GET /company?id=XX&country_iso=YY`. This API endpoint receives the parameters id and country_iso. `id` can be a string without any particular limitation. `country_iso` will be a two-letter country code to select the backend according to the application configuration. The solution must query the backend in a proper country and return:
    - An HTTP 200/OK reply when the company with the requested id exists on the corresponding backend. The body should be a JSON object with the company data returned by a backend.
    - An HTTP 404/Not Found reply if the company with the requested id does not exist on the corresponding backend.

Application should always reply with the following JSON object to the customer:

```json
{
  "id": "string, the company id requested by a customer",
  "name": "string, the company name, as returned by a backend",
  "active": "boolean, indicating if the company is still active according to the active_until date",
  "active_until": "RFC 3339 UTC date-time expressed as a string, optional."
}
```

## Backend providers API description

Backendify is designed to work with two existing backend API versions, V1 and V2, as the ecosystem transitions. It transparently handles the differences between them.

Both backends will answer HTTP GET requests on the `/companies/<id>` path, where `id` is the arbitrary string. However, their replies are slightly different:

1. V1 backend will return the JSON object of the following format:

```json
{
  "cn": "string, the company name.",
  "created_on": "RFC3339 UTC datetime expressed as a string.",
  "closed_on": "RFC3339 UTC datetime expressed as a string, optional.",
}
```

2. V1 backend reply will have a Content-Type of an `application/x-company-v1`.

3. V2 backend will return the JSON object of the following format:
```json
{
  "company_name": "string, the company name.",
  "tin": "string, tax identification number",
  "dissolved_on": "RFC3339 UTC datetime expressed as a string, optional.",
}
```

4. V2 backend reply will have a different Content-Type of an `application/x-company-v2`.

## Prerequisites

Before you can build and run Backendify, you'll need:
*   A C++ compiler supporting C++17 (e.g., GCC, Clang, MSVC).
*   CMake (version 3.15 or higher).
*   `vcpkg` for C++ package management. Ensure it's correctly bootstrapped and integrated with your build environment.

## Key Dependencies

Backendify relies on several powerful open-source libraries:
*   **Boost C++ Libraries (Asio & Beast):** Form the core of the asynchronous networking capabilities. Asio provides the event loop and I/O services, while Beast handles HTTP protocol details.
*   **nlohmann/json:** A versatile library for JSON parsing and serialization.
*   **hiredis:** A minimalistic C client library for Redis, used by `RedisCache`.
*   **googletest:** For writing and running unit tests.

These dependencies are managed using `vcpkg`. Refer to `backendify\CODE_DESIGN.md` for a complete list and rationale.

## Building the Project

To build the application, follow these steps:

```bash
# Navigate to the project directory
cd backendify

# Clean previous build (optional, but good practice)
# For Linux/macOS:
# rm -rf build
# For Windows (PowerShell):
Remove-Item -Recurse -Force build

# Create and navigate to the build directory
mkdir build
cd build

# Configure the project with CMake, pointing to the vcpkg toolchain file
cmake .. -DCMAKE_TOOLCHAIN_FILE=.../backendify/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build the project (e.g., in Release mode)
cmake --build . --config Release
```
The executable `backendify.exe` (or `backendify` on Linux/macOS) will be located in `backendify\build\Release\`.

## Configuration

Backendify's behavior can be tailored through a configuration file and command-line arguments.

*   **Configuration File (`backendify\backendify.config`):**
    This file allows you to set parameters like the listening port, Redis connection details, log levels, metrics settings, etc. Values in this file override the hardcoded defaults. The structure of configurable parameters is defined in `backendify\src\config\AppConfig.hpp`.

*   **Command-Line Arguments:**
    These are primarily used for defining the mappings between ISO country codes and their corresponding backend server addresses.
    For example: `US=http://127.0.0.1:9001 EU=http://127.0.0.1:9002`

*   **Configuration Precedence:**
    1.  Hardcoded Defaults (in `AppConfig.hpp`).
    2.  Values from `backendify\backendify.config`.
    3.  Command-line Arguments (country-to-backend mappings are *only* configurable via command-line).

## Running the Application

Once built, you can run Backendify from the project's root directory:

```bash
cd backendify
.\build\Release\backendify.exe US=http://127.0.0.1:9001 ru=http://localhost:9002
```

Replace `US=http://127.0.0.1:9001` and `ru=http://localhost:9002` with your actual country-to-backend mappings. The application listens on port 9000 by default, but this can be changed in `backendify\backendify.config`.

## Testing the Application

### Running Unit Tests

Unit tests are written using Google Test and can be run via CTest:

```bash
# Navigate to the build directory
cd backendify\build

# Run all tests with output on failure
ctest --output-on-failure

# Run a specific test (or pattern) with verbose output
ctest -R "TestNamePattern" -V
```

### Manual Testing with cURL

You can test the running application using `curl` or any HTTP client:

*   **Check Status:**
    ```bash
    curl "http://localhost:9000/status"
    ```

*   **Get Company Data:**
    ```bash
    curl "http://localhost:9000/company?id=<company_id>&country_iso=US"
    ```
    (Replace `<company_id>` with an actual ID and `US` with a configured country ISO code).

The `backendify\BackendServers\` directory contains a prototype backend HTTP server that can be useful for E2E testing during development.

## Key Design Decisions & Implementation Details

Backendify incorporates several important design choices for robustness and performance:

*   **Architecture:** The `master` branch features a fully asynchronous, event-driven architecture built with Boost.Asio (for the I/O event loop) and Boost.Beast (for HTTP protocol handling). This allows efficient management of many concurrent connections with a limited number of threads.
*   **Caching Strategy:**
    *   **Redis (`RedisCache`):** Primary cache for production, offering persistence and shared caching.
    *   **In-Memory (`InMemoryCache`):** Fallback if Redis is unavailable, also used for unit tests. Implements an LRU eviction policy and has a configurable size limit.
    *   **Time-To-Live (TTL):** Cache entries are stored for 24 hours by default, as backend data changes infrequently.
*   **Logging:** An extensible logging system (`ILogger` interface) with a `ConsoleLogger` implementation. Log levels (`DEBUG`, `INFO`, `WARN`, `ERROR`, `SETUP`) are configurable.
*   **Metrics Collection:** An `IStatsDClient` interface with a `StatsDClient` (sends UDP packets to a StatsD server) and a `DummyStatsDClient` (no-op). The application uses the real client if the `STATSD_SERVER` environment variable is set.
*   **Error Handling:**
    *   Returns appropriate HTTP 4xx errors for invalid client requests.
    *   Uses `504 Gateway Timeout` for backend communication failures (after a retry).
    *   Uses `502 Bad Gateway` for invalid/unexpected backend responses.
    *   Implements a simple time-based circuit breaker for backends returning `503 Service Unavailable`.
*   **Performance and Concurrency:**
    *   Utilizes a configurable thread pool to service the Boost.Asio `io_context`.
    *   Employs non-blocking I/O for all network operations.
    *   Features session-level rate limiting/load shedding for outgoing responses to prioritize fresh requests under load.

For a much more detailed explanation of these aspects, including dependencies, specific library choices, performance considerations, and debugging strategies, please refer to the **`backendify\CODE_DESIGN.md`** document.

## Production Considerations

*   **SLA Expectation:** The target is for 95% of requests to be replied to within 1 second. Aggressive client retries are expected if this SLA is not met.
*   **Caching:** Crucial for meeting SLAs, especially with unreliable backends. Data is cached for 24 hours.
*   **Performance Tuning:** The `backendify\CODE_DESIGN.md` file contains an extensive section on "Debugging / Performance Tuning in Production," outlining strategies for metrics-based tuning, log analysis, and optimization, especially relevant for the asynchronous architecture.

## Current Limitations

*   **HTTP Only:** The application currently supports only HTTP for both incoming client connections and outgoing backend connections. HTTPS is not yet implemented.

## Next Steps

We have a few exciting enhancements planned:

1.  **Implement a rate limiting solution:** We're exploring options like mfycheng/ratelimiter.
2.  **Set up custom CI/CD pipelines on GitLab.**

We hope this guide helps you get started with Backendify!
