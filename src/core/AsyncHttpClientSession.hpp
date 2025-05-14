#ifndef ASYNC_HTTP_CLIENT_SESSION_HPP
#define ASYNC_HTTP_CLIENT_SESSION_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <chrono> // For timeouts

#include "../config/AppConfig.hpp" // For BackendUrlInfo
#include "../interfaces/ILogger.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class AsyncHttpClientSession : public std::enable_shared_from_this<AsyncHttpClientSession> {
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_; // Must persist for reads
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;
    const BackendUrlInfo& backend_info_;
    std::function<void(http::response<http::string_body>, beast::error_code)> on_complete_;
    std::shared_ptr<ILogger> logger_; // Optional: for more detailed logging
    net::steady_timer timer_;

public:
    AsyncHttpClientSession(
        net::io_context& ioc,
        const BackendUrlInfo& backendInfo,
        const std::string& target_path,
        std::chrono::milliseconds timeout,
        std::function<void(http::response<http::string_body>, beast::error_code)> on_complete,
        std::shared_ptr<ILogger> logger = nullptr)
        : resolver_(net::make_strand(ioc)),
          stream_(net::make_strand(ioc)),
          backend_info_(backendInfo),
          on_complete_(std::move(on_complete)),
          logger_(logger),
          timer_(ioc) {
        req_.version(11); // HTTP/1.1
        req_.method(http::verb::get);
        req_.target(target_path);
        req_.set(http::field::host, backend_info_.backend_host);
        req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req_.set(http::field::accept_encoding, "gzip, deflate"); // Example: request compression
        req_.prepare_payload(); // Important if there was a body

        // Set overall timeout for the operation
        timer_.expires_after(timeout);
    }

    void run() {
        // Start the timer
        timer_.async_wait(beast::bind_front_handler(&AsyncHttpClientSession::on_timeout, shared_from_this()));
        do_resolve();
    }

    void cancel() {
        // Cancel the timer and close the socket.
        // This should cause any pending async operations to complete with an error (e.g., operation_aborted).
        timer_.cancel();
        beast::error_code ec;
        stream_.socket().close(ec); // Close the underlying socket
    }

private:
    void on_timeout(beast::error_code ec) {
        if (ec && ec != net::error::operation_aborted) { // Timer expired
            if(logger_) logger_->error("AsyncHttpClientSession timeout for " + backend_info_.url);
            beast::error_code close_ec;
            stream_.socket().close(close_ec); // Close the socket
            return on_complete_({}, beast::errc::make_error_code(beast::errc::timed_out));
        }
    }

    void do_resolve() {
        resolver_.async_resolve(
            backend_info_.backend_host,
            std::to_string(backend_info_.backend_port),
            beast::bind_front_handler(&AsyncHttpClientSession::on_resolve, shared_from_this()));
    }

    void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) return on_complete_({}, ec);
        beast::get_lowest_layer(stream_).async_connect(
            results,
            beast::bind_front_handler(&AsyncHttpClientSession::on_connect, shared_from_this()));
    }

    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
        if (ec) return on_complete_({}, ec);
        // Note: HTTPS would require SSL handshake here
        http::async_write(stream_, req_,
            beast::bind_front_handler(&AsyncHttpClientSession::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) return on_complete_({}, ec);
        http::async_read(stream_, buffer_, res_,
            beast::bind_front_handler(&AsyncHttpClientSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        timer_.cancel(); // Operation completed, cancel the timer
        boost::ignore_unused(bytes_transferred);

        beast::error_code shut_ec;
        stream_.socket().shutdown(tcp::socket::shutdown_both, shut_ec);
        if (shut_ec && shut_ec != beast::errc::not_connected) {
            if(logger_) logger_->error("AsyncHttpClientSession shutdown error: " + shut_ec.message());
        }

        // If ec is set, and it's not end_of_stream, it's likely a more serious read error.
        on_complete_(std::move(res_), ec == http::error::end_of_stream ? beast::error_code{} : ec);
    }
};

#endif // ASYNC_HTTP_CLIENT_SESSION_HPP