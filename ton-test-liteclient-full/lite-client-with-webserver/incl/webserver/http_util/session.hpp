#pragma once

#include <memory>

namespace SimpleWeb{
    class Connection;
    class Request;

    /* In cause small functional , can be deleted in future */
    class Session {
    public:
        Session(std::size_t max_request_streambuf_size, std::shared_ptr<Connection> connection_) noexcept;
        ~Session() = default;

        Session(const Session&)= delete;
        Session& operator=(const Session&)= delete;

        std::shared_ptr<Connection> get_connection() const;
        std::shared_ptr<Request>    get_request() const;
    private:
        std::shared_ptr<Connection> connection;
        std::shared_ptr<Request> request;
    };
}