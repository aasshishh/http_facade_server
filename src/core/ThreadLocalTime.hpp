#ifndef THREADLOCALTIME_HPP
#define THREADLOCALTIME_HPP

#include <chrono>

// Functions to access the thread-local enqueue time defined in main.cpp
void set_current_request_enqueue_time(std::chrono::steady_clock::time_point t);
std::chrono::steady_clock::time_point get_current_request_enqueue_time();

#endif // THREADLOCALTIME_HPP