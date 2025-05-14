# Code Organization

*   `backendify/`
    *   `backendify.config`        # Configuration Params
    *   `src/`
        *   `main.cpp`             # Entry point
        *   `cache/`               # Caching implementations (RedisCache, InMemoryCache)
        *   `config/`              # Configuration loading (AppConfig)
        *   `core/`                # Main application logic (Backendify)
        *   `interfaces/`          # Abstract interfaces (ILogger, ICache, IStatsDClient)
        *   `logging/`             # Logging implementations (ConsoleLogger)
        *   `metrics/`             # StatsD/metrics implementations (StatsDClient, DummyStatsDClient)
        *   `models/`              # Data structures (CompanyInfo, BackendUrlInfo)
        *   `third_party/`         # Third-party library headers (httplib.h, json.hpp, UDPSender.hpp)
        *   `utils/`               # Utility functions (parsing, time conversion, etc.)
    *   `build/`                   # Build output (executables, libraries)
    *   `tests/`                   # Unit Tests (`gtest` based)
    *   `vcpkg/`                   # Dependency manager files
    *   `BackendServers`           # Backend HTTP Server Prototype to support E2E testing while development
    *   `CODE_DESIGN.md`           # Design Decisions
    *   `CMakeLists.txt`           # Main CMake build script
    *   `Dockerfile`               # Docker build instructions
    *   `README.md`                # Project overview and instructions


# BUILD
* `cd backendify`
* if Linux: 
`rm -rf build`
* if Windows: 
`Remove-Item -Recurse -Force build`
* `mkdir build`
* `cd build`
* `cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake`
* `cmake --build . --config Release`

# RUN UNIT TESTS
*   `cd backendify/build`
*   Run a test : `ctest -R "TestNamePattern" -V`
*   Run all tests : `ctest --output-on-failure`

# RUN
*   `cd backendify`
* `.\build\Release\backendify.exe US=http://127.0.0.1:9001`

# EDIT CONFIGURATIONS
*   `AppConfig.hpp`: Defines the structure holding all configuration parameters.
*   `backendify.config`: Primary file for setting runtime configurations (e.g., ports, Redis details, log level). Values here override hardcoded defaults set during loading.
*   `Command-line arguments`: Used exclusively for country-to-backend mappings.

# TEST
* Sample Command : `curl "http://localhost:9000/status"`
* Sample Command : `curl "http://localhost:9000/company?id=<company_id>&country_iso=US"`

# DESIGN DECISIONS

**Dependencies:**
*   *Boost C++ Libraries*: [Boost C++ Libraries](https://www.boost.org/) were selected for their robust, feature-rich, and peer-reviewed components, forming the foundation of the application's asynchronous networking capabilities.
    *   *`Boost.Asio`*: The cornerstone of the asynchronous architecture. It provides a powerful and versatile model for network and low-level I/O programming, using an `io_context` to manage operations and dispatch completion handlers. Its flexible threading options, including strands, are leveraged for efficient and safe concurrent programming.
    *   *`Boost.Beast`*: A header-only library built upon Boost.Asio, providing low-level HTTP/1 protocol support. It's used for constructing, parsing, and transmitting HTTP messages, integrating seamlessly with the Asio asynchronous model.
*   *`nlohmann/json`*: Chosen for its modern C++ approach, intuitive API, and comprehensive features for JSON parsing and serialization.
*   *`hiredis`*: The official, lightweight C client library for Redis, selected for its directness and performance in implementing the `RedisCache`.
*   *`googletest`*: A widely-adopted C++ testing framework, employed for its rich assertion set and features for writing comprehensive unit tests.
*   *`vcpkg`*: Utilized as the C++ package manager to simplify the acquisition and management of third-party library dependencies across different development and build environments.

**Third Party Libs:**
* *`nlohmann/json.hpp`*
* *`[UDPSender.hpp](https://github.com/vthiery/cpp-statsd-client/blob/master/include/cpp-statsd-client/UDPSender.hpp)`*

**Caching Strategy:**
* Implemented two caching mechanisms: Redis (`RedisCache`) and In-Memory (`InMemoryCache`).
* *`RedisCache`* is the primary cache intended for production runs, leveraging Redis for persistence and shared caching if multiple instances were run.
* *`InMemoryCache`* serves as a fallback if Redis is unavailable and is also used during unit testing for simplicity and isolation.
* The `InMemoryCache` implements a LRU (Least Recently Used) eviction policy and has a configurable maximum size limit to prevent unbounded memory growth in fallback scenarios.
* *`Time-To-Live (TTL)`* : Cache entries have a TTL as specified in the requirements (currently defaulting to 24 hours).

**Logging Framework:**
* An extensible logging system is built around the `ILogger` interface.
* Currently, a `ConsoleLogger` implementation is provided and used, directing logs to the standard output/error streams.
* Supports multiple log levels (`DEBUG`, `INFO`, `WARN`, `ERROR`, `SETUP`, etc.) controllable via configuration, allowing different verbosity for development, testing, and production environments.
* The logger implementation (`ConsoleLogger`) is thread-safe.

**Metrics Collection:**
* Includes an interface (`IStatsDClient`) for sending metrics.
* Provides two implementations: `StatsDClient` for sending UDP packets to a StatsD-compatible server and `DummyStatsDClient` which performs no operations.
* The application checks the `STATSD_SERVER` environment variable at startup. If found, it attempts to use the real `StatsDClient`; otherwise, it falls back to the `DummyStatsDClient`, ensuring the application runs even without a metrics server.
* The `StatsDClient` delegates the actual UDP sending to an internal `UDPSender` instance. The `UDPSender` resolves the destination address once during construction and handles batching metrics (using an internal mutex-protected queue) before periodically flushing them to the `STATSD_SERVER`. Batching behavior is controlled via `metrics_batch_size` & `metrics_send_interval` configuration.

**Configuration Management:**
* Core settings (*like listening port, Redis details, log level, metrics_batch_size, metrics_send_interval*) are managed via an external configuration file (`backendify.config`).
* This allows users to modify behavior without recompiling the application.
* Country-to-backend mappings are currently provided only via command-line arguments.

**Configuration Precedence:**
1.  *Hardcoded Defaults*: Initial values set within the `AppConfig` class constructor.
2.  *Config File (`backendify.config`)*: Values read from the configuration file override the hardcoded defaults.
3.  *Command-line Arguments*: Arguments passed at startup can override both defaults and config file settings. (Note: Country mappings are *only* configurable via command-line arguments currently).

**API Implementation:**
* The server listens on the configured port (defaulting to 9000).
* Implements the required `GET /status` endpoint for health checks.
* Implements the `GET /company?id=XX&country_iso=YY` endpoint, handling routing to the correct backend based on `country_iso`, interacting with the cache, parsing V1/V2 backend responses, and formatting the final JSON output according to specifications.

**Error Handling:**
* Reference : [HTTP response status codes](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status)
* Handles invalid client requests (e.g., missing `id` or `country_iso` parameters, unknown `country_iso`) by returning appropriate HTTP 4xx errors (e.g., 400 Bad Request).
* Backend communication failures (timeouts, connection errors after retry) typically result in a `504 Gateway Timeout`.
* Invalid or unexpected responses from backends (e.g., non-200/404 status, parsing failures) typically result in a `502 Bad Gateway`.
* A single retry attempt is made for backend API calls if the initial attempt fails with a potentially transient network error (e.g., timeout, connection error).
* Includes robust parsing of backend responses (both V1 and V2 formats), validating expected content types and JSON structures. If a backend returns unexpected or malformed data, the error is logged.
* A simple `time-based circuit breaker` is implemented: if a backend returns a 503 Service Unavailable, subsequent calls to that specific backend are blocked for a specific time period *(default 10 milliseconds)* to allow recovery, returning a `504` to the client during this cooldown.

**Performance and Concurrency:**
*   **Fully Asynchronous, Event-Driven Architecture (Boost.Asio & Boost.Beast):** The application has been re-architected to leverage a fully asynchronous, event-driven model using Boost.Asio for the core I/O event loop and Boost.Beast for HTTP protocol handling. This marks a fundamental shift from a thread-per-request model.
    *   **Event Loop (`io_context`):** A central `boost::asio::io_context` manages all asynchronous operations. A pool of worker threads runs this `io_context`, allowing for concurrent processing of I/O events.
    *   **Non-Blocking I/O:** All network operations (accepting client connections, reading requests, writing responses, making backend API calls) are non-blocking. Operations return immediately, and their completion is handled by callbacks (completion handlers) posted to the `io_context`. This allows a small number of threads to efficiently manage a large number of concurrent connections and I/O operations.
    *   **Session-Based State Management:** Each client connection (`HttpServerSession`) and each backend API call (`AsyncHttpClientSession`) manages its own state through a series of chained asynchronous operations and their completion handlers. This effectively creates a state machine for each concurrent operation, pausing when waiting for I/O and resuming when the operation completes.
    *   **Reduced Threading Overhead:** Compared to a thread-per-request model, this architecture significantly reduces the number of threads required, leading to lower context-switching overhead and reduced memory consumption per connection.
    *   **Scalability:** This model is inherently more scalable, capable of handling a higher degree of I/O concurrency.
*   **Thread Pool for `io_context`:** The number of threads servicing the `io_context` is configurable (via `number_of_threads_per_core` in `AppConfig.hpp`, multiplied by `std::thread::hardware_concurrency()`), aiming to match available CPU cores for optimal processing of I/O completion handlers.
*   **SLA-Aware Response Queuing (Session-Level Rate Limiting):** Each `HttpServerSession` employs an internal queue for outgoing responses, acting as a session-level rate limiter or load shedding mechanism. This queue allows `Backendify` to process incoming requests and generate responses potentially faster than they can be written to the network, which is beneficial for HTTP pipelining and handling bursty traffic. The queue has a configurable maximum size (`max_response_queue_size`). Critically, if this queue becomes full, the oldest (and thus closest to potential SLA violation) responses are discarded to make space for newer ones. This strategy prioritizes fresh requests, increasing the likelihood of meeting the overall SLA for a higher percentage of client interactions, especially under sustained load.
*   **Asynchronous Backend Client:** The `AsyncHttpClientSession` handles calls to backend servers asynchronously, including DNS resolution, connection, request writing, and response reading, all managed by the `io_context`.
*   **Shared Resource Safety:** Shared resources like the logger, cache, and metrics client are designed to be safe for concurrent access from multiple completion handlers running on different `io_context` threads, primarily through thread-safe implementations or careful synchronization where necessary (e.g., `StatsDClient`'s internal queue).
*   **Timeouts:** Asynchronous timers (`boost::asio::steady_timer`) are used extensively to manage timeouts for client connections, individual backend API calls, and even specific asynchronous write operations within `HttpServerSession` to prevent indefinite blocking.

**Testing Strategy:**
* Unit tests are implemented using the Google Test framework (`gtest`).
* Tests focus on individual components like utility functions (date parsing, configuration loading), cache implementations, and backend response parsing logic.
* Dependencies on external services are managed during testing by using the `InMemoryCache` (instead of Redis) and the `DummyStatsDClient` (instead of a real StatsD server).
* Tests for backend interactions often use mock HTTP servers (provided by `httplib`'s test utilities or custom setups) to simulate V1 and V2 backend responses, including error cases.

**Limitations:**
*   **HTTP Only (No HTTPS):** The application currently supports only HTTP for incoming client connections and outgoing backend connections. Implementing HTTPS, while beneficial for security, would introduce additional complexity (e.g., certificate management, SSL/TLS handshake overhead) and is not part of the current scope but could be considered for future enhancements if required.

**Debugging / Performance Tuning in Production:**
*   **Acknowledge Constraints:** Production / Canary offers limited resources. Debugging relies heavily on indirect observation and metrics.
*   **Enhanced Contextual Understanding:** While production access is limited, gaining deeper insights into client expectations (e.g., specific failure definitions, how SLAs are precisely measured by clients), detailed API contracts with backend servers (including their rate limits, error semantics, and expected performance characteristics), and the overall production architecture (e.g., load balancer behavior, network topology between services, resource allocation policies) would significantly aid in proactive design improvements, more accurate local simulations, and more effective troubleshooting.
*   **Additional Metrics:** If within limits, we can consider adding targeted metrics to pinpoint issues, such as:
    *   A gauge for the size of the `InMemoryCache` (if Redis fails).
    *   Timers for critical code sections (e.g., time spent in queue, backend call duration, response parsing duration) to identify performance bottlenecks and guide configuration adjustments.
    *   Counters for specific error types (as defined in `MetricsDefinitions`) to help identify single points of failure (SPOF) or common error patterns.
    *   Metrics related to thread pool activity (e.g., queue length, active threads) to help optimize the number of worker threads.
*   **Log Analysis (Canary & Ideal Production):** Analyzing detailed logs from canary deployments is crucial for identifying crashes, single points of failure (SPOF), and performance chokepoints. While direct, comprehensive production log access is currently unavailable, having some form of sampled or aggregated production logs would be highly beneficial for these purposes.
*   **Structured Logging:** Emphasize structured logging (e.g., JSON format) in canary/staging to allow for easier parsing, querying, and analysis with log management tools, especially for tracing requests or identifying error patterns.
*   **External Monitoring:** Use Kubernetes/orchestrator tools (`kubectl top pod`, infrastructure monitoring like Prometheus/Grafana if available) to track the container's actual CPU and Memory consumption. Confirm OOM kills by checking container exit codes (137) and resource usage graphs.
*   **Load Balancer Metrics:** Review metrics from the load balancer (e.g., request rates per instance, connection counts, health check status) to identify instance-specific issues or traffic distribution problems.
*   **Alerting:** Set up alerts on key metrics (e.g., high error rates, SLA breaches, resource utilization approaching limits, frequent circuit breaker tripping) to proactively identify and respond to issues.
*   **Load Testing in Staging:** Simulate production load and unreliability (e.g., slow/failing backends, unavailable Redis) in a staging environment with identical resource constraints. This helps trigger OOMs or performance bottlenecks before hitting production.
*   **Local Profiling:** Before deploying significant changes, use local profiling tools (Valgrind/Massif for memory, perf for CPU) under simulated load/error conditions to identify potential leaks or performance hotspots that might only manifest under pressure.
*   **Iterative Configuration Tuning:** Based on metrics and monitoring, adjust configuration parameters (e.g., thread count, backend timeouts, `InMemoryCache` size) and redeploy to observe the impact on stability and performance.
*   **Optimize CPU-Intensive Operations:** Identify and optimize CPU-bound tasks within I/O completion handlers to prevent starving the event loop. Areas to review include:
    *   Manual query parameter parsing loops.
    *   JSON parsing (`nlohmann::json::parse`) and serialization (`.dump()`).
    *   Complex logic within cache interactions.
    *   Consider offloading very heavy CPU work to a separate thread pool if necessary.
*   **Minimize String Manipulation Overhead:** Reduce frequent string allocations and copies, which can impact performance.
    *   Utilize `std::string_view` where appropriate for non-owning string access.
    *   Pre-allocate string capacity (e.g., with `reserve()`) when building strings if the approximate size is known.
    *   Evaluate efficient string formatting alternatives (e.g., `fmtlib` or C++20 `std::format`) for performance-critical paths.
*   **Analyze Mutex Contention:** While current mutex usage (e.g., for `active_client_sessions_` or `tripped_backends_`) is likely not a bottleneck, monitor for potential contention under extreme load. If identified by profiling, explore more granular locking or concurrent data structures.
*   **Leverage Profiling Tools:** Systematically use profiling tools to pinpoint actual performance bottlenecks ("hot spots") rather than relying on assumptions.
    *   **Linux:** `perf` (for CPU, events), `gprof` (call graph), Valgrind (`callgrind` for CPU, `cachegrind` for cache simulation, `Massif` for heap).
    *   **Windows:** Visual Studio's built-in performance profiler, Intel VTune Profiler.
    *   Focus optimization efforts on areas identified by these tools for the most significant impact.