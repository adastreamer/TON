#pragma once

#include <memory>

namespace SimpleWeb{
    class Connection;
    class Request;

    class Session {
    public:
        Session(std::size_t max_request_streambuf_size, std::shared_ptr<Connection> connection_) noexcept;
        ~Session() = default;

        std::shared_ptr<Connection> connection;
        std::shared_ptr<Request> request;
    };
}