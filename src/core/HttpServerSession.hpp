#ifndef HTTP_SERVER_SESSION_HPP
#define HTTP_SERVER_SESSION_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>
#include <boost/config.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream> // For std::cerr in destructor fallback
#include <memory>
#include <sstream>
#include <string>
#include <deque> // Changed from <vector>
#include <typeinfo>
#include <optional> // For std::optional

#include "../config/AppConfig.hpp"
#include "../interfaces/ILogger.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Forward declaration
class Backendify; 

class HttpServerSession : public std::enable_shared_from_this<HttpServerSession> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<Backendify> backendify_service_; // To handle requests
    std::shared_ptr<ILogger> logger_;
    const AppConfig& config_;
    std::function<void(std::shared_ptr<HttpServerSession>)> on_finish_callback_; // Called when session is done

    // The queue for outgoing responses.
    std::deque<std::shared_ptr<http::response<http::string_body>>> response_queue_; // Changed to std::deque

    // Indicates if a write operation is currently in progress.
    bool write_in_progress_ = false;

    // Timer for diagnosing stuck async_write operations.
    net::steady_timer diagnostic_write_timer_;
    // To coordinate between async_write completion and diagnostic timer expiration for the *current* write.
    std::atomic<bool> current_write_op_completed_{false}; 

    // Temporary storage for the request, cleared before each read.
    http::request<http::string_body> req_;
    std::chrono::steady_clock::time_point request_received_time_;

public:
    HttpServerSession(
        tcp::socket&& socket,
        std::shared_ptr<Backendify> service,
        std::shared_ptr<ILogger> logger,
        const AppConfig& config,
        std::function<void(std::shared_ptr<HttpServerSession>)> on_finish)
        : stream_(std::move(socket)),
          backendify_service_(service),
          logger_(logger),
          config_(config),
          on_finish_callback_(std::move(on_finish)),
          diagnostic_write_timer_(stream_.get_executor()) // Initialize with stream's executor
    {
        std::ostringstream oss;
        oss << static_cast<void*>(this);
        if (logger_) { // Defensively check logger
            logger_->debug("HttpServerSession " + oss.str() + " CONSTRUCTOR called.");
        }
    }

    ~HttpServerSession() {
        std::ostringstream oss;
        oss << static_cast<void*>(this);
        // Check if logger_ is valid before using, in case of destruction order issues
        if (logger_) {
            logger_->debug("HttpServerSession " + oss.str() + " DESTRUCTOR called.");
        } else {
            // Fallback to cerr if logger is gone, though this indicates another problem
            std::cerr << "HttpServerSession " << oss.str() << " DESTRUCTOR called (logger unavailable)." << std::endl;
        }
        // Ensure timer is cancelled if session is destroyed.
        // No error code from cancel() here, it throws on failure.
        diagnostic_write_timer_.cancel();
    }

    void run() {
        std::ostringstream oss;
        oss << static_cast<void*>(this);
        logger_->debug("HttpServerSession " + oss.str() + " run() called. Dispatching do_read.");

        net::dispatch(stream_.get_executor(),
                      beast::bind_front_handler(
                          &HttpServerSession::do_read,
                          shared_from_this()));
    }

    void stop() {
        std::ostringstream oss;
        oss << static_cast<void*>(this);
        logger_->debug("HttpServerSession " + oss.str() + " stop() called.");

        // Cancel diagnostic timer. The zero-argument version throws on error.
        current_write_op_completed_.store(true);
        diagnostic_write_timer_.cancel();

        // This is a simple stop. More robust would be to cancel pending async ops on the stream.
        // For now, just close the socket which should cause pending ops to complete with an error.
        beast::error_code ec_socket;
        if (stream_.socket().is_open()) {
            stream_.socket().shutdown(tcp::socket::shutdown_both, ec_socket); // Shutdown first
            if (ec_socket && ec_socket != beast::errc::not_connected) {
                 logger_->error("HttpServerSession " + oss.str() + " socket shutdown error during stop: " + ec_socket.message());
            }
            ec_socket = {}; // Reset error code for close
            stream_.socket().close(ec_socket); // Then close
        }
        
        if (ec_socket && ec_socket != beast::errc::not_connected) { // not_connected can be normal if already closed
            logger_->error("HttpServerSession " + oss.str() + " socket close error during stop: " + ec_socket.message());
        } else if (!ec_socket) {
            logger_->debug("HttpServerSession " + oss.str() + " socket closed successfully during stop.");
        }
    }

private:
    void do_read() {
        std::ostringstream oss;
        oss << static_cast<void*>(this);
        logger_->debug("HttpServerSession " + oss.str() + " do_read() called.");

        request_received_time_ = std::chrono::steady_clock::now();
        req_ = {}; // Clear previous request
        stream_.expires_after(std::chrono::seconds(30)); // Set a timeout for reading request

        http::async_read(stream_, buffer_, req_,
                         beast::bind_front_handler(
                             &HttpServerSession::on_read,
                             shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        std::ostringstream oss;
        oss << static_cast<void*>(this);

        if (ec == http::error::end_of_stream) {
            logger_->debug("HttpServerSession " + oss.str() + " on_read: end_of_stream. Calling do_close.");
            return do_close();
        }

        if (ec) {
            if (ec == net::error::operation_aborted) {
                logger_->debug("HttpServerSession " + oss.str() + " on_read: operation aborted (likely due to stop() or timer). Calling do_close.");
            } else {
                logger_->error("HttpServerSession " + oss.str() + " on_read error: " + ec.message());
            }
            return do_close(); // Close on any read error
        }

        logger_->debug("HttpServerSession " + oss.str() + " on_read successful. Bytes: " + std::to_string(bytes_transferred) + ". Handling request for target: " + std::string(req_.target()));
        
        // Pass request to Backendify service
        handle_request(std::move(req_));
    }

    // Implemented in HttpServerSession.cpp
    void handle_request(http::request<http::string_body>&& req); 

    // Called by Backendify's completion handlers to queue a response.
    // If opt_res is std::nullopt, the request is dropped and no response is sent.
    // The session will then either read the next request or close.
    void send_response(std::optional<http::response<http::string_body>>&& opt_res);

    void do_write() {
        std::ostringstream oss_session;
        oss_session << static_cast<void*>(this);

        if (response_queue_.empty()) {
            write_in_progress_ = false; // Should not happen if called correctly
            logger_->error("HttpServerSession " + oss_session.str() + "::do_write called with empty queue.");
            return;
        }
        
        write_in_progress_ = true;
        auto self = shared_from_this();

        auto current_response_ptr = response_queue_.front();

        logger_->debug("HttpServerSession " + oss_session.str() + "::do_write - preparing to call http::async_write for queued response. Status: " + std::to_string(current_response_ptr->result_int()));

        // Pre-write checks
        if (!stream_.socket().is_open()) {
            logger_->error("HttpServerSession " + oss_session.str() + "::do_write - SOCKET IS NOT OPEN before async_write. Clearing queue and closing.");
            response_queue_.clear();
            write_in_progress_ = false;
            return do_close();
        }
        current_write_op_completed_.store(false); // Reset for this new write operation
        logger_->debug("HttpServerSession " + oss_session.str() + "::do_write - Initiating diagnostic timer (5s).");
        // Start diagnostic timer
        diagnostic_write_timer_.expires_after(std::chrono::seconds(5)); // Shorter timeout: 5 seconds
        diagnostic_write_timer_.async_wait(
            [self, session_addr_str = oss_session.str()](beast::error_code ec_timer) {            
            // IMPORTANT: Check if current_write_op_completed_ is true. If so, the write already finished (or errored).
            if (self->current_write_op_completed_.load()) {
                // self->logger_->debug("HttpServerSession " + session_addr_str + " diagnostic_write_timer: current_write_op_completed_ was true, timer action skipped.");
                return;
            }
            if (ec_timer == net::error::operation_aborted) {
                // Timer was cancelled, meaning async_write completed or session stopped/closed.
                self->logger_->debug("HttpServerSession " + session_addr_str + " diagnostic_write_timer cancelled (operation_aborted).");
            } else if (ec_timer) {
                // Some other error with the timer itself
                self->logger_->error("HttpServerSession " + session_addr_str + " diagnostic_write_timer error: " + ec_timer.message());
            } else { // Timer expired
                // Timer expired! This means async_write's completion handler was not called within 5s.
                self->logger_->error("HttpServerSession " + session_addr_str + " CRITICAL: diagnostic_write_timer EXPIRED (5s). async_write completion handler did not run. Attempting to close session socket.");
                std::cerr << "HttpServerSession " << session_addr_str << " CRITICAL: diagnostic_write_timer EXPIRED (5s). async_write completion handler did not run." << std::endl;
                boost::system::error_code close_ec;
                std::cerr << "[SESS_TIMER_DEBUG] HttpServerSession " << session_addr_str << " diagnostic_write_timer EXPIRED (std::cerr)." << std::endl;
                if (self->stream_.socket().is_open()) { // Check before closing
                    self->stream_.socket().close(close_ec); 
                }
                if(close_ec) {
                    self->logger_->error("HttpServerSession " + session_addr_str + " diagnostic timer: error closing socket: " + close_ec.message());
                } else {
                     self->logger_->debug("HttpServerSession " + session_addr_str + " diagnostic timer: socket closed.");
                }
            }
        });

        http::async_write(stream_, *current_response_ptr,
            beast::bind_front_handler(
                &HttpServerSession::on_write,
                shared_from_this(),
                current_response_ptr->keep_alive() // Pass keep_alive decision
            ));
    }

    void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        std::ostringstream oss_session;
        oss_session << static_cast<void*>(this);
        if (logger_->getLogLevel() <= LogUtils::LogLevel::DEBUG) {
            logger_->debug("[ASYNC_WRITE_DEBUG] HttpServerSession " + oss_session.str() + " on_write completion handler entered (std::cerr). EC: " + ec.message() + ", KeepAlive: " + (keep_alive ? "true" : "false"));
        }

        // Mark write operation as completed and cancel timer
        // exchange returns the previous value.
        bool timer_already_fired_and_acted = current_write_op_completed_.exchange(true);
        diagnostic_write_timer_.cancel(); // Cancel regardless, it's cheap if already fired/cancelled.

        if (timer_already_fired_and_acted) {
            logger_->error("HttpServerSession " + oss_session.str() + " on_write handler: current_write_op_completed_ was already true (timer likely fired and acted). EC: " + ec.message() + ". Bytes: " + std::to_string(bytes_transferred));
            if (ec) {
                // handle operation_aborted correctly.
                return do_close(); 
            }
            logger_->error("HttpServerSession " + oss_session.str() + " on_write: No error, but timer previously fired. Closing session.");
            return do_close();
        }

        // Handle actual write error
        if (ec) {
            if (ec == net::error::operation_aborted) {
                logger_->debug("HttpServerSession " + oss_session.str() + " on_write: operation aborted (likely due to stop() or timer). Calling do_close.");
            } else {
                logger_->error("HttpServerSession " + oss_session.str() + " on_write error: " + ec.message() + ". Bytes transferred: " + std::to_string(bytes_transferred));
            }
            response_queue_.clear(); // Don't try to send more from queue on error
            write_in_progress_ = false;
            return do_close();
        }

        logger_->debug("HttpServerSession " + oss_session.str() + " on_write successful. Bytes transferred: " + std::to_string(bytes_transferred) + ". Keep-Alive: " + (keep_alive ? "yes" : "no"));
        
        // Remove the successfully sent response from the queue
        if (!response_queue_.empty()) {
            response_queue_.pop_front();
        } else {
            // Should not happen if logic is correct
            logger_->error("HttpServerSession " + oss_session.str() + " on_write: response_queue_ was empty after successful write. This is unexpected.");
        }

        // If there are more responses in the queue, send the next one.
        if (!response_queue_.empty()) {
            logger_->debug("HttpServerSession " + oss_session.str() + " on_write: more responses in queue, calling do_write again.");
            do_write(); // This will set write_in_progress_ = true again
        } else {
            // No more responses in the queue.
            write_in_progress_ = false;
            if (!keep_alive) {
                logger_->debug("HttpServerSession " + oss_session.str() + " on_write: not keep-alive and queue empty, calling do_close.");
                return do_close();
            }
            // Keep-alive: read the next request.
            logger_->debug("HttpServerSession " + oss_session.str() + " on_write: keep-alive and queue empty, calling do_read for next request.");
            do_read();
        }
    }

    void do_close() {
        std::ostringstream oss;
        oss << static_cast<void*>(this);
        logger_->debug("HttpServerSession " + oss.str() + "::do_close called.");

        // Mark operation as completed to prevent diagnostic timer from acting if it fires late.
        // Also, cancel the timer explicitly.
        current_write_op_completed_.store(true); 
        diagnostic_write_timer_.cancel(); 

        boost::system::error_code ec_shutdown;
        // Check if socket is open before trying to shut down or close
        if (stream_.socket().is_open()) {
            // Send a TCP shutdown
            stream_.socket().shutdown(tcp::socket::shutdown_send, ec_shutdown);
        }
        
        if (ec_shutdown && ec_shutdown != beast::errc::not_connected) { 
            logger_->error("HttpServerSession " + oss.str() + " socket shutdown warning in do_close (may be expected): " + ec_shutdown.message());
        }
        
        // Notify the listener that this session is finished
        if (on_finish_callback_) {
            logger_->debug("HttpServerSession " + oss.str() + "::do_close - dispatching on_finish_callback_.");
            // Dispatch to avoid re-entrancy and ensure it runs on the strand
            // The callback itself might destroy the last shared_ptr to this session.
            auto cb = std::move(on_finish_callback_); // Move out before calling
            on_finish_callback_ = nullptr; // Prevent multiple calls
            
            net::dispatch(stream_.get_executor(), beast::bind_front_handler(std::move(cb), shared_from_this()));
        }
    }
};

#endif // HTTP_SERVER_SESSION_HPP