#include "BeastHttpServer.hpp"
#include "HttpServerSession.hpp" // Ensure this is included
#include "../core/Backendify.hpp" // For Backendify service

void BeastHttpServer::do_accept() {
    acceptor_.async_accept(
        net::make_strand(ioc_), // Ensure handlers run on a strand if ioc is multi-threaded
        beast::bind_front_handler(
            &BeastHttpServer::on_accept,
            shared_from_this()));
}

void BeastHttpServer::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        this->logger_->error("BeastHttpServer accept error: " + ec.message());
        // Don't stop accepting on recoverable errors
        if (ec != net::error::operation_aborted) {
             do_accept(); // Continue accepting
        }
        return;
    }

    // Create the session and run it
    // std::make_shared<HttpServerSession>(std::move(socket), this->backendify_service_, this->logger_)->run();
    auto session = std::make_shared<HttpServerSession>(
        std::move(socket), 
        this->backendify_service_, 
        this->logger_,
        this->config_,
        [this](std::shared_ptr<HttpServerSession> session_to_remove) {
            this->on_session_finish(session_to_remove); 
        } // Completion handler
    );
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        active_sessions_.insert(session);
        std::ostringstream oss;
        oss << static_cast<void*>(session.get());
        logger_->debug("BeastHttpServer::on_accept - created and added session " + oss.str() + " to active set.");
    }
    
    // Log before session->run()
    std::ostringstream oss_run;
    oss_run << static_cast<void*>(session.get());
    logger_->debug("BeastHttpServer::on_accept - calling run() for session " + oss_run.str());

    session->run();

    this->do_accept(); // Accept another connection
}

void BeastHttpServer::on_session_finish(std::shared_ptr<HttpServerSession> session) {
    // Create a string representation of the pointer for logging
    std::ostringstream oss;
    oss << static_cast<void*>(session.get());
    logger_->debug("BeastHttpServer::on_session_finish - ENTERING for session " + oss.str());
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    size_t erased_count = active_sessions_.erase(session);
    if (erased_count > 0) {
        logger_->debug("BeastHttpServer::on_session_finish - successfully removed session " + oss.str() + " from active set. Active sessions count: " + std::to_string(active_sessions_.size()));
    } else {
        // This might indicate a double removal or a session not properly added.
        logger_->error("BeastHttpServer::on_session_finish - session " + oss.str() + " was NOT FOUND in active set for removal. Active sessions count: " + std::to_string(active_sessions_.size()));
    }
}