#ifndef BEAST_HTTP_SERVER_HPP
#define BEAST_HTTP_SERVER_HPP

#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>

#include "../interfaces/ILogger.hpp"
#include "../config/AppConfig.hpp"
#include "HttpServerSession.hpp"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Forward declaration
class Backendify;

class BeastHttpServer : public std::enable_shared_from_this<BeastHttpServer> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<Backendify> backendify_service_;
    std::shared_ptr<ILogger> logger_;
    const AppConfig& config_;
    std::mutex sessions_mutex_;
    std::unordered_set<std::shared_ptr<HttpServerSession>> active_sessions_;

public:
    BeastHttpServer(
        net::io_context& ioc,
        tcp::endpoint endpoint,
        std::shared_ptr<Backendify> service,
        std::shared_ptr<ILogger> logger,
        const AppConfig& config)
        : ioc_(ioc),
          acceptor_(ioc),
          backendify_service_(service),
          logger_(logger),
          config_(config) {
        beast::error_code ec;

        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            logger_->error("BeastHttpServer open acceptor error: " + ec.message());
            throw std::runtime_error("Failed to open acceptor: " + ec.message());
        }

        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            logger_->error("BeastHttpServer set_option error: " + ec.message());
            throw std::runtime_error("Failed to set_option: " + ec.message());
        }

        acceptor_.bind(endpoint, ec);
        if (ec) {
            logger_->error("BeastHttpServer bind error: " + ec.message());
            throw std::runtime_error("Failed to bind: " + ec.message());
        }

        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            logger_->error("BeastHttpServer listen error: " + ec.message());
            throw std::runtime_error("Failed to listen: " + ec.message());
        }
    }

    void run() {
        do_accept();
    }

    void stop() {
        logger_->info("BeastHttpServer stopping...");
        beast::error_code ec;
        acceptor_.cancel(ec); // Cancel pending async_accept
        if (ec) logger_->error("BeastHttpServer acceptor cancel error: " + ec.message());
        acceptor_.close(ec);  // Close the acceptor
        if (ec) logger_->error("BeastHttpServer acceptor close error: " + ec.message());
        logger_->info("BeastHttpServer stopped accepting new connections.");

        // Cancel active sessions
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (const auto& session_ptr : active_sessions_) {
            if (session_ptr) session_ptr->stop(); // Add stop() to HttpServerSession
        }
        active_sessions_.clear();
    }

private:
    void do_accept(); // Implementation will create HttpServerSession
    void on_accept(beast::error_code ec, tcp::socket socket); // Declare on_accept
    void on_session_finish(std::shared_ptr<HttpServerSession> session);
};

#endif // BEAST_HTTP_SERVER_HPP