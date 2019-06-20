#pragma once


#include <string>
#include <boost/asio.hpp>
#include "utility.hpp"
#include "macro.hpp"

namespace SimpleWeb{
    class Content : public std::istream {
    public:
        Content(asio::streambuf &streambuf) noexcept : std::istream(&streambuf), streambuf(streambuf) {}

        std::size_t size() noexcept {
            return streambuf.size();
        }
        /// Convenience function to return std::string. The stream buffer is consumed.
        std::string string() noexcept {
            try {
                std::string str;
                auto size = streambuf.size();
                str.resize(size);
                read(&str[0], static_cast<std::streamsize>(size));
                return str;
            }
            catch(...) {
                return std::string();
            }
        }

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