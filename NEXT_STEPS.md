# Next Steps

1.  Implement a rate limiting solution.
    *   The [mfycheng/ratelimiter](https://github.com/mfycheng/ratelimiter) repository is a good candidate to explore for this.
2.  Set up custom CI/CD pipelines on GitLab.
3.  **Enhance Redis Concurrency:** To improve Redis performance and allow multiple asynchronous accesses, consider replacing the current synchronous `hiredis` setup. C++ Redis client libraries designed for concurrency, such as `redis-plus-plus` or `Boost.Redis`, offer better efficiency through asynchronous I/O or connection pooling.
    *   **`redis-plus-plus`**: A feature-rich client supporting various modes (sync, async, cluster, sentinel) and integrating with event loops like Boost.Asio or libuv.
    *   **`Boost.Redis`**: A header-only, Boost.Asio-based client, ideal for projects already using Boost.
    These libraries enable multiple "in-flight" operations, avoiding the serialization imposed by a single connection with a mutex. Adopting one would involve refactoring the `RedisCache` to use its API, leveraging its built-in concurrency management.
**Experiment with Memcached:** Investigate using Memcached as an alternative caching backend. This could offer performance benefits and more straightforward asynchronous access compared to the current Redis cache, which relies on the synchronous `hiredis` client.

## References
*   [A list of open-source C++ libraries](https://en.cppreference.com/w/cpp/links/libs)
