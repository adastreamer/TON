#include <webserver/http_util/request.hpp>


namespace SimpleWeb{
    Content::Content(asio::streambuf &streambuf) noexcept : std::istream(&streambuf), streambuf(streambuf) {}


    std::size_t Content::size() noexcept {
        return streambuf.size();
    }
    /// Convenience function to return std::string. The stream buffer is consumed.
    std::string Content::string() noexcept {
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


    Request::Request(std::size_t max_request_streambuf_size, std::shared_ptr<asio::ip::tcp::endpoint> remote_endpoint_) noexcept
            : streambuf(max_request_streambuf_size), content(streambuf), remote_endpoint(std::move(remote_endpoint_)) {}


    std::string Request::remote_endpoint_address() const noexcept {
        try {
            return remote_endpoint->address().to_string();
        }
        catch(...) {
            return std::string();
        }
    }

    unsigned short Request::remote_endpoint_port() const noexcept {
        return remote_endpoint->port();
    }

/// Returns query keys with percent-decoded values.
    CaseInsensitiveMultimap Request::parse_query_string() const noexcept {
        return SimpleWeb::QueryString::parse(query_string);
    }
}


