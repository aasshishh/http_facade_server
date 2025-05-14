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
* `cmake --build . --config Release -DENABLE_THREAD_SANITIZER=OFF`

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
*    *`cpp-httplib`:* Selected for its simplicity, ease of integration (header-only), and straightforward API for both HTTP server and client functionalities. Its blocking, thread-per-request model was deemed suitable for the initial synchronous implementation, offering a good balance between development speed and reasonable performance for moderate loads. It provided a quick way to get a functional HTTP server and client up and running without introducing complex asynchronous programming models early on.
*    *`nlohmann/json`:* Chosen for its intuitive API for JSON parsing and serialization.
*    *`hiredis`:* The official C client library for Redis, used by the `RedisCache` implementation.
*    *`googletest`:* Employed as the framework for writing and running unit tests.
*    *`vcpkg`:* Used for managing C++ library dependencies across platforms.

**Third Party Libs:**
* *`cpp-httplib`*
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
* The HTTP server (`httplib::Server`) is configured to use its built-in thread pool (`httplib::ThreadPool`) to handle multiple incoming client requests concurrently. Each incoming request is typically assigned to a dedicated thread from this pool for the duration of its processing.
* The number of threads in the pool is configurable (via `number_of_threads_per_core` in `AppConfig.hpp`, multiplied by `std::thread::hardware_concurrency()`) and defaults to a value based on the available hardware cores. This aims to maximize CPU utilization without excessive context switching.
* **Synchronous Operations:** Within each request-handling thread, operations such as backend API calls (`httplib::Client::Get`) are performed synchronously (blocking). This means the thread waits for the backend response before proceeding.
* Shared resources accessed by concurrent request handlers (e.g., logger, cache, metrics client) have been implemented to be thread-safe (using mutexes where necessary) to prevent race conditions and ensure data integrity under load.
* Each thread maintains `one client instance per backend`, stored in `thread_local` storage.
* The primary contention point for shared resources under high load is expected to be the internal mutex within the single `UDPSender` instance used for batching metrics, though this is likely minor unless metric generation is extremely high.
* Backend API calls made via `httplib::Client` are configured with `connection_timeout_in_microseconds` and `read_request_timeout_in_microseconds` to prevent indefinite blocking on unresponsive backends.

**Testing Strategy:**
* Unit tests are implemented using the Google Test framework (`gtest`).
* Tests focus on individual components like utility functions (date parsing, configuration loading), cache implementations, and backend response parsing logic.
* Dependencies on external services are managed during testing by using the `InMemoryCache` (instead of Redis) and the `DummyStatsDClient` (instead of a real StatsD server).
* Tests for backend interactions often use mock HTTP servers (provided by `httplib`'s test utilities or custom setups) to simulate V1 and V2 backend responses, including error cases.

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
