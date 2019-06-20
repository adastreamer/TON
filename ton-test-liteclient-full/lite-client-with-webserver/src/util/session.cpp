#include <webserver/http_util/session.hpp>
#include <webserver/http_util/connection.hpp>
#include <webserver/http_util/request.hpp>
#include <webserver/http_util/macro.hpp>

namespace SimpleWeb{
    Session::Session(std::size_t max_request_streambuf_size, std::shared_ptr<Connection> connection_) noexcept:
    connection(std::move(connection_)) {
        if(!this->connection->remote_endpoint) {
         error_code ec;
         this->connection->remote_endpoint = std::make_shared<asio::ip::tcp::endpoint>(
                 this->connection->socket->lowest_layer().remote_endpoint(ec));
        }
        request = std::shared_ptr<Request>(new Request(max_request_streambuf_size, this->connection->remote_endpoint));
    }
}

