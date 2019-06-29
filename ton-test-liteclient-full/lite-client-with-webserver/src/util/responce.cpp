#include <webserver/http_util/responce.hpp>
#include <webserver/http_util/session.hpp>
#include <webserver/http_util/connection.hpp>

namespace SimpleWeb{
    Response::Response(std::shared_ptr<Session> session_, long timeout_content) noexcept :
    std::ostream(nullptr),
    session(std::move(session_)), timeout_content(timeout_content),
    strand(session->get_connection()->socket->get_io_service())
    {
        rdbuf(streambuf.get());
    }

    Response::~Response() {}

    std::size_t Response::size() noexcept {
        return streambuf->size();
    }

    /// Use this function if you need to recursively send parts of a longer message, or when using server-sent events (SSE).
    void Response::send(const std::function<void(const error_code &)> &callback) noexcept {
        session->get_connection()->set_timeout(timeout_content);

        std::shared_ptr<asio::streambuf> streambuf = std::move(this->streambuf);
        this->streambuf = std::unique_ptr<asio::streambuf>(new asio::streambuf());
        rdbuf(this->streambuf.get());

        auto self = this->shared_from_this();
        strand.post([self, streambuf, callback]() {
            self->send_queue.emplace_back(streambuf, callback);
            if(self->send_queue.size() == 1)
                self->send_from_queue();
        });
    }

    /// Write directly to stream buffer using std::ostream::write
    void Response::write(const char_type *ptr, std::streamsize n) {
        std::ostream::write(ptr, n);
    }

    /// Convenience function for writing status line, potential header fields, and empty content
    void Response::write(StatusCode status_code, const CaseInsensitiveMultimap& header) {
        *this << "HTTP/1.1 " << SimpleWeb::status_code(status_code) << "\r\n";
        write_header(header, 0);
    }

    /// Convenience function for writing status line, header fields, and content
    void Response::write(StatusCode status_code, string_view content, const CaseInsensitiveMultimap& header
                                    ) {
        *this << "HTTP/1.1 " << SimpleWeb::status_code(status_code) << "\r\n";
        write_header(header, content.size());
        if(!content.empty())
            *this << content;
    }

    /// Convenience function for writing status line, header fields, and content
    void Response::write(StatusCode status_code, std::istream &content, const CaseInsensitiveMultimap &header) {
        *this << "HTTP/1.1 " << SimpleWeb::status_code(status_code) << "\r\n";
        content.seekg(0, std::ios::end);
        auto size = content.tellg();
        content.seekg(0, std::ios::beg);
        write_header(header, size);
        if(size)
            *this << content.rdbuf();
    }

    /// Convenience function for writing success status line, header fields, and content
    void Response::write(string_view content, const CaseInsensitiveMultimap &header) {
        write(StatusCode::success_ok, content, header);
    }

    /// Convenience function for writing success status line, header fields, and content
    void Response::write(std::istream &content, const CaseInsensitiveMultimap &header) {
        write(StatusCode::success_ok, content, header);
    }

    /// Convenience function for writing success status line, and header fields
    void Response::write(const CaseInsensitiveMultimap &header) {
        write(StatusCode::success_ok, std::string(), header);
    }


    void Response::send_from_queue() {
        auto self = this->shared_from_this();
        strand.post([self]() {
            asio::async_write(*self->session->get_connection()->socket,
                    *self->send_queue.begin()->first, self->strand.wrap([self](const error_code &ec, std::size_t /*bytes_transferred*/) {
                auto lock = self->session->get_connection()->handler_runner->continue_lock();
                if(!lock)
                    return;
                if(!ec) {
                    auto it = self->send_queue.begin();
                    if(it->second)
                        it->second(ec);
                    self->send_queue.erase(it);
                    if(self->send_queue.size() > 0)
                        self->send_from_queue();
                }
                else {
                    // All handlers in the queue is called with ec:
                    for(auto &pair : self->send_queue) {
                        if(pair.second)
                            pair.second(ec);
                    }
                    self->send_queue.clear();
                }
            }));
        });
    }

    void Response::send_on_delete(const std::function<void(const error_code &)> &callback) noexcept  {
        session->get_connection()->set_timeout(timeout_content);
        auto self = this->shared_from_this(); // Keep Response instance alive through the following async_write
        asio::async_write(*session->get_connection()->socket, *streambuf, [self, callback]
                (const error_code &ec, std::size_t /*bytes_transferred*/) {
            self->session->get_connection()->cancel_timeout();
            auto lock = self->session->get_connection()->handler_runner->continue_lock();
            if(!lock)
                return;
            if(callback)
                callback(ec);
        });
    }

}