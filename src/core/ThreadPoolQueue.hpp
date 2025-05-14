#include <deque>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include "ThreadLocalTime.hpp"

#include "../config/AppConfig.hpp"
#include "../interfaces/ILogger.hpp"
#include "../interfaces/IStatsDClient.hpp"
#include "../third_party/httplib.h"

// Structure to hold task and its enqueue time
struct TimedTask {
    std::function<void()> task;
    std::chrono::steady_clock::time_point enqueued_time;
};

// Custom Task Queue implementing httplib's interface
class ThreadPoolQueue : public httplib::TaskQueue {
public:
    ThreadPoolQueue(
        size_t thread_count, 
        std::shared_ptr<ILogger> logger)
        : logger_(logger), 
        shutdown_(false) {
        threads_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            threads_.emplace_back([this] { worker_thread(); });
        }
        logger_->setup("ThreadPoolQueue initialized with " + std::to_string(thread_count) + " threads");
    }

    ~ThreadPoolQueue() override {
        shutdown();
    }

    // Enqueue a task with the current timestamp
    bool enqueue(std::function<void()> fn) override {
        if (shutdown_) {
            logger_->error("Attempted to enqueue task on shutdown queue.");
            return false; // Indicate failure to enqueue
        }
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            task_queue_.push_back({std::move(fn), std::chrono::steady_clock::now()});
        }
        cv_.notify_one();
        return true; // Indicate successful enqueue
    }

    // Signal threads to stop and join them
    void shutdown() override {
        if (shutdown_.exchange(true)) {
             return; // Already shutting down
        }
        logger_->debug("Shutting down ThreadPoolQueue...");
        cv_.notify_all(); // Wake up all waiting threads
        for (std::thread& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        logger_->debug("ThreadPoolQueue shut down complete.");
    }

private:
    void worker_thread() {
        while (true) {
            TimedTask current_task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                // Wait until queue is not empty OR shutdown is requested
                cv_.wait(lock, [this] { return !task_queue_.empty() || shutdown_; });

                if (shutdown_ && task_queue_.empty()) {
                    return; // Exit thread if shutdown and queue is empty
                }

                // Check if shutdown was requested while waiting
                if (shutdown_) {
                     if (task_queue_.empty()) return; // Double check if queue emptied during shutdown signal
                }

                current_task = std::move(task_queue_.front());
                task_queue_.pop_front();
            } // queue_mutex_ unlocked here
            
            // Execute the task
            try {
                // Sleep : for testing that this works as intended
                // std::this_thread::sleep_for(std::chrono::milliseconds(20));
                set_current_request_enqueue_time(current_task.enqueued_time);
                current_task.task();
            } catch (const std::exception& e) {
                logger_->error("Exception caught in worker thread task: " + std::string(e.what()));
            } catch (...) {
                logger_->error("Unknown exception caught in worker thread task.");
            }
        }
    }

    std::shared_ptr<ILogger> logger_;
    std::deque<TimedTask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> threads_;
    std::atomic<bool> shutdown_;
};
