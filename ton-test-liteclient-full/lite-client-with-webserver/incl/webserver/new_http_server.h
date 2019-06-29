#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <thread>

#include "http_util/utility.hpp"
#include "http_util/macro.hpp"



namespace SimpleWeb{
    class Connection;
    class Response;
    class Session;
    class Request;

    class CustomHttpServer{
    public:
        CustomHttpServer(const unsigned short port);
        ~CustomHttpServer();

        CustomHttpServer(const CustomHttpServer&) = delete;
        CustomHttpServer& operator=(const CustomHttpServer&) = delete;
        CustomHttpServer(const CustomHttpServer&&) = delete;



        unsigned short bind();

        /// If you know the server port in advance, use start() instead.
        /// Accept requests, and if io_service was not set before calling bind(), run the internal io_service instead.
        /// Call after bind().
        void accept_and_run();

        /// Start the server by calling bind() and accept_and_run()
        void start();

        /// Stop accepting new requests, and close current connections.
        void stop() noexcept;
    private:
        class regex_orderable : public regex::regex {
        public:
            std::string str;

            regex_orderable(const char *regex_cstr) : regex::regex(regex_cstr), str(regex_cstr) {}
            regex_orderable(std::string regex_str_) : regex::regex(regex_str_), str(std::move(regex_str_)) {}
            bool operator<(const regex_orderable &rhs) const noexcept {
                return str < rhs.str;
            }
        };


        class Config {
        public:
            Config(unsigned short port) noexcept : port(port) {}

            /// Port number to use. Defaults to 80 for HTTP and 443 for HTTPS. Set to 0 get an assigned port.
            unsigned short port;
            /// If io_service is not set, number of threads that the server will use when start() is called.
            /// Defaults to 1 thread.
            std::size_t thread_pool_size = 1;
            /// Timeout on request handling. Defaults to 5 seconds.
            long timeout_request = 5;
            /// Timeout on content handling. Defaults to 300 seconds.
            long timeout_content = 300;
            /// Maximum size of request stream buffer. Defaults to architecture maximum.
            /// Reaching this limit will result in a message_size error code.
            std::size_t max_request_streambuf_size = std::numeric_limits<std::size_t>::max();
            /// IPv4 address in dotted decimal form or IPv6 address in hexadecimal notation.
            /// If empty, the address will be any address.
            std::string address;
            /// Set to false to avoid binding the socket to an address that is already in use. Defaults to true.
            bool reuse_address = true;
            /// Make use of RFC 7413 or TCP Fast Open (TFO)
            bool fast_open = false;
        };

    public:
        std::map<regex_orderable,std::map<std::string,
                std::function<void(std::shared_ptr<Response>,
                                   std::shared_ptr<Request>)>>> resource;

        std::map<std::string, std::function<void(std::shared_ptr<Response>,
                                                 std::shared_ptr<Request>)>> default_resource;

        std::function<void(std::shared_ptr<Request>, const error_code &)> on_error;

        std::function<void(std::unique_ptr<asio::ip::tcp::socket> &, std::shared_ptr<Request>)> on_upgrade;

        Config config;
    private:

        void after_bind() {}
        void accept();

        template <typename... Args>
        std::shared_ptr<Connection> create_connection(Args &&... args) noexcept;

        void read(const std::shared_ptr<Session> &session);

        void read_chunked_transfer_encoded(const std::shared_ptr<Session> &session,
                                           const std::shared_ptr<asio::streambuf> &chunks_streambuf);

        void read_chunked_transfer_encoded_chunk(const std::shared_ptr<Session> &session,
                                                 const std::shared_ptr<asio::streambuf> &chunks_streambuf,
                                                 unsigned long length);

        void find_resource(const std::shared_ptr<Session> &session);

        void write(const std::shared_ptr<Session> &session,
                   std::function<void(std::shared_ptr<Response>,
                                      std::shared_ptr<Request>)> &resource_function);
    private:
        bool internal_io_service = false;
        std::shared_ptr<asio::io_service> io_service;

        std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
        std::vector<std::thread> threads;

        std::shared_ptr<std::unordered_set<Connection *>> connections;
        std::shared_ptr<std::mutex> connections_mutex;

        std::shared_ptr<ScopeRunner> handler_runner;
    };
}

