#include "HttpServerSession.hpp"
#include "Backendify.hpp" // Include Backendify to call its methods
#include <sstream>

// This method is called from on_read when a request has been successfully read.
void HttpServerSession::handle_request(http::request<http::string_body>&& req) {
    std::ostringstream oss;
    oss << static_cast<void*>(this);
    logger_->debug("HttpServerSession " + oss.str() + " handle_request for target: " + std::string(req.target()));
    if (!backendify_service_) {
        logger_->error("HttpServerSession " + oss.str() + ": Backendify service is null.");
        // Send an internal server error response
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = "Internal server error: service not available.";
        res.prepare_payload();
        logger_->debug("HttpServerSession " + oss.str() + " handle_request - sending 500 due to null service.");
        return send_response(std::move(res)); // Use the new send_response to queue it
    }

    std::string_view target_path = req.target();
    size_t query_pos = target_path.find('?');
    if (query_pos != std::string_view::npos) {
        target_path = target_path.substr(0, query_pos);
    }

    if (req.method() == http::verb::get && target_path == "/company") {
        logger_->debug("HttpServerSession " + oss.str() + " handle_request - routing to processCompanyRequest.");
        // Backendify service will call our send_response method via this callback
        backendify_service_->processCompanyRequest(std::move(req_), request_received_time_, // Pass req_ (member)
        [self = shared_from_this()](std::optional<http::response<http::string_body>> opt_res_from_backendify) { // Capture self
            std::ostringstream cb_oss;
            cb_oss << static_cast<void*>(self.get());
            self->logger_->debug("HttpServerSession " + cb_oss.str() + " handle_request (company) - callback received, calling send_response to queue.");
            self->send_response(std::move(opt_res_from_backendify));
        });
    } else if (req.method() == http::verb::get && target_path == "/status") {
        logger_->debug("HttpServerSession " + oss.str() + " handle_request - routing to processStatusRequest.");
        backendify_service_->processStatusRequest(
            [self = shared_from_this()](std::optional<http::response<http::string_body>> opt_res_from_backendify) { // Capture self
                std::ostringstream cb_oss;
                cb_oss << static_cast<void*>(self.get());
                self->logger_->debug("HttpServerSession " + cb_oss.str() + " handle_request (status) - callback received, calling send_response to queue.");
                self->send_response(std::move(opt_res_from_backendify));
            });
    } else {
        logger_->debug("HttpServerSession " + oss.str() + " handle_request - target not found: " + std::string(req_.target())); // Use req_
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(req.target()) + "' was not found.";
        res.prepare_payload();
        logger_->debug("HttpServerSession " + oss.str() + " handle_request - sending 404.");
        return send_response(std::make_optional(std::move(res)));
    }
}

void HttpServerSession::send_response(std::optional<http::response<http::string_body>>&& opt_res) {
    std::ostringstream oss_session;
    oss_session << static_cast<void*>(this);

    if (opt_res) {
        if (response_queue_.size() >= config_.max_response_queue_size) {
            // Make space by removing the oldest item from the front of the queue.
            auto& oldest_response_ptr = response_queue_.front();
            if (logger_->getLogLevel() <= LogUtils::LogLevel::WARN) {
                logger_->warn("HttpServerSession " + oss_session.str() +
                    "::send_response: Response queue is full (current size " +
                    std::to_string(response_queue_.size()) + ", max " +
                    std::to_string(config_.max_response_queue_size) +
                    "). Discarding oldest response (status " +
                    std::to_string(oldest_response_ptr->result_int()) +
                    ") to make space for new response for target '" +
                    std::string(req_.target()) + "' (status " +
                    std::to_string(opt_res->result_int()) + ").");
            }
            response_queue_.pop_front(); // Remove the oldest response
        }
    }

    if (!opt_res) {
        logger_->warn("HttpServerSession " + oss_session.str() + "::send_response: received nullopt. Request for target '" + std::string(req_.target()) + "' will be dropped.");
        if (write_in_progress_) {
            logger_->debug("HttpServerSession " + oss_session.str() + "::send_response: request dropped. A write is already in progress for a previous response. Letting it complete.");
            return; 
        }

        // No write in progress. This dropped request was the only pending one, or the last in a pipeline.
        bool current_req_keep_alive = req_.keep_alive();
        if (current_req_keep_alive) {
            logger_->debug("HttpServerSession " + oss_session.str() + "::send_response: request dropped. Keep-alive. Calling do_read() for next request.");
            return do_read();
        } else {
            logger_->debug("HttpServerSession " + oss_session.str() + "::send_response: request dropped. Not keep-alive. Calling do_close().");
            return do_close();
        }
    }

    logger_->debug("HttpServerSession " + oss_session.str() + "::send_response called to queue response. Status: " + std::to_string(opt_res->result_int()));
    auto response_ptr = std::make_shared<http::response<http::string_body>>(std::move(*opt_res));
    response_queue_.push_back(response_ptr);

    if (!write_in_progress_) {
        do_write();
    }
}