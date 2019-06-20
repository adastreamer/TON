#include <webserver/http_util/connection.hpp>

namespace SimpleWeb{


    void Connection::close() noexcept {
        error_code ec;
        socket->lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket->lowest_layer().cancel(ec);
    }

    void Connection::set_timeout(long seconds) noexcept {
        if(seconds == 0) {
            timer = nullptr;
            return;
        }
        /* get_io_context was removed in 1.70 boost */
        timer = std::unique_ptr<asio::steady_timer>(new asio::steady_timer(socket->get_io_service()));
        timer->expires_from_now(std::chrono::seconds(seconds));
        auto self = this->shared_from_this();
        timer->async_wait([self](const error_code &ec) {
            if(!ec)
                self->close();
        });
    }

    void Connection::cancel_timeout() noexcept {
        if(timer) {
            error_code ec;
            timer->cancel(ec);
        }
    }
}