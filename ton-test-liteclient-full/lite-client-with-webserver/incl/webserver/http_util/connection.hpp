#pragma once

#include "macro.hpp"
#include "utility.hpp"

namespace SimpleWeb{
    class Connection : public std::enable_shared_from_this<Connection> {
    public:
        template <typename... Args>
        Connection(std::shared_ptr<ScopeRunner> handler_runner_, Args &&... args) noexcept :
                handler_runner(std::move(handler_runner_)),
                socket(new asio::ip::tcp::socket(std::forward<Args>(args)...)) {}

        ~Connection()  = default;

        void close() noexcept;

        void set_timeout(long seconds) noexcept;

        void cancel_timeout() noexcept ;
    public:
        std::shared_ptr<ScopeRunner> handler_runner;

        std::unique_ptr<asio::ip::tcp::socket> socket; // Socket must be unique_ptr since asio::ssl::stream<asio::ip::tcp::socket> is not movable

        std::unique_ptr<asio::steady_timer> timer;

        std::shared_ptr<asio::ip::tcp::endpoint> remote_endpoint;
    };
}

