#pragma once

#include <list>
#include <boost/asio.hpp>

#include "utility.hpp"
#include "macro.hpp"

namespace asio = boost::asio;

namespace SimpleWeb{
    class Session;
    class Response : public std::enable_shared_from_this<Response>, public std::ostream {
    public:
        Response(std::shared_ptr<Session> session_, long timeout_content) noexcept;
        ~Response();


        std::size_t size() noexcept;

        /// Use this function if you need to recursively send parts of a longer message, or when using server-sent events (SSE).
        void send(const std::function<void(const error_code &)> &callback = nullptr) noexcept;

        /// Write directly to stream buffer using std::ostream::write
        void write(const char_type *ptr, std::streamsize n) ;

        /// Convenience function for writing status line, potential header fields, and empty content
        void write(StatusCode status_code = StatusCode::success_ok, const CaseInsensitiveMultimap& header =
                CaseInsensitiveMultimap());

        /// Convenience function for writing status line, header fields, and content
        void write(StatusCode status_code, string_view content, const CaseInsensitiveMultimap &header
                                                                            = CaseInsensitiveMultimap());

        /// Convenience function for writing status line, header fields, and content
        void write(StatusCode status_code, std::istream &content, const CaseInsensitiveMultimap &header
                                                                            = CaseInsensitiveMultimap());

        /// Convenience function for writing success status line, header fields, and content
        void write(string_view content, const CaseInsensitiveMultimap &header = CaseInsensitiveMultimap());

        /// Convenience function for writing success status line, header fields, and content
        void write(std::istream &content, const CaseInsensitiveMultimap &header = CaseInsensitiveMultimap());

        /// Convenience function for writing success status line, and header fields
        void write(const CaseInsensitiveMultimap &header);

        /// If true, force server to close the connection after the response have been sent.
        ///
        /// This is useful when implementing a HTTP/1.0-server sending content
        /// without specifying the content length.
        bool close_connection_after_response = false;

        void send_on_delete(const std::function<void(const error_code &)> &callback = nullptr) noexcept;
        std::shared_ptr<Session> getSession()const {
            return session;
        }

    private:
        template <typename size_type>
        void write_header(const CaseInsensitiveMultimap &header, size_type size) {
            bool content_length_written = false;
            bool chunked_transfer_encoding = false;
            for(auto &field : header) {
                if(!content_length_written && case_insensitive_equal(field.first, "content-length"))
                    content_length_written = true;
                else if(!chunked_transfer_encoding && case_insensitive_equal(field.first, "transfer-encoding") && case_insensitive_equal(field.second, "chunked"))
                    chunked_transfer_encoding = true;

                *this << field.first << ": " << field.second << "\r\n";
            }
            if(!content_length_written && !chunked_transfer_encoding && !close_connection_after_response)
                *this << "Content-Length: " << size << "\r\n\r\n";
            else
                *this << "\r\n";
        }

        void send_from_queue();

    private:

        std::unique_ptr<asio::streambuf> streambuf = std::unique_ptr<asio::streambuf>(new asio::streambuf());

        std::shared_ptr<Session> session;
        long timeout_content;

        asio::io_service::strand strand;
        std::list<std::pair<std::shared_ptr<asio::streambuf>,
                std::function<void(const error_code &)>>> send_queue;
    };
}