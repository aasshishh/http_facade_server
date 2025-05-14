#include "ThreadLocalTime.hpp"

// Define the thread_local variable and its accessors here
thread_local std::chrono::steady_clock::time_point current_request_enqueue_time;

void set_current_request_enqueue_time(std::chrono::steady_clock::time_point t) {
    current_request_enqueue_time = t;
}

std::chrono::steady_clock::time_point get_current_request_enqueue_time() {
    return current_request_enqueue_time;
}