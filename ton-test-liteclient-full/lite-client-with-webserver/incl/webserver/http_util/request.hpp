#pragma once


#include <string>
#include <boost/asio.hpp>
#include "utility.hpp"
#include "macro.hpp"

namespace SimpleWeb{
    class Content : public std::istream {
    public:
        Content(asio::streambuf &streambuf) noexcept;
        ~Content() = default;

        std::size_t size() noexcept;
        /// Convenience function to return std::string. The stream buffer is consumed.
        std::string string() noexcept;

    private:
        asio::streambuf &streambuf;
    };

    class Request {
    public:
        Request(std::size_t max_request_streambuf_size,
                std::shared_ptr<asio::ip::tcp::endpoint> remote_endpoint_) noexcept;

        ~Request() = default;

        std::string remote_endpoint_address() const noexcept ;

        unsigned short remote_endpoint_port() const noexcept;

        /// Returns query keys with percent-decoded values.
        CaseInsensitiveMultimap parse_query_string() const noexcept;
    public:

        /* TODO as getters and setters in future */

        std::string method, path, query_string, http_version;

        Content content;

        CaseInsensitiveMultimap header;

        regex::smatch path_match;

        std::shared_ptr<asio::ip::tcp::endpoint> remote_endpoint;

        /// The time point when the request header was fully read.
        std::chrono::system_clock::time_point header_read_time;

        asio::streambuf streambuf;
    };
}