#include <webserver/http_server.hpp>


ServerBase::ServerBase(unsigned short port) noexcept : config(port),
                                           connections(new std::unordered_set<Connection *>()),
                                           connections_mutex(new std::mutex()),
                                           handler_runner(new ScopeRunner()) {}

void ServerBase::after_bind() {}


template <typename... Args>
std::shared_ptr<Connection> ServerBase::create_connection(Args &&... args) noexcept {
    auto connections = this->connections;
    auto connections_mutex = this->connections_mutex;
    auto connection = std::shared_ptr<Connection>(new Connection(handler_runner, std::forward<Args>(args)...), [connections, connections_mutex](Connection *connection) {
        {
            std::lock_guard<std::mutex> lock(*connections_mutex);
            auto it = connections->find(connection);
            if(it != connections->end())
                connections->erase(it);
        }
        delete connection;
    });
    {
        std::lock_guard<std::mutex> lock(*connections_mutex);
        connections->emplace(connection.get());
    }
    return connection;
}

void ServerBase::read(const std::shared_ptr<Session> &session) {
    session->connection->set_timeout(config.timeout_request);
    asio::async_read_until(*session->connection->socket, session->request->streambuf, "\r\n\r\n", [this, session](const error_code &ec, std::size_t bytes_transferred) {
        session->connection->cancel_timeout();
        auto lock = session->connection->handler_runner->continue_lock();
        if(!lock)
            return;
        session->request->header_read_time = std::chrono::system_clock::now();
        if((!ec || ec == asio::error::not_found) && session->request->streambuf.size() == session->request->streambuf.max_size()) {
            auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
            response->write(StatusCode::client_error_payload_too_large);
            if(this->on_error)
                this->on_error(session->request, make_error_code::make_error_code(errc::message_size));
            return;
        }
        if(!ec) {
            // request->streambuf.size() is not necessarily the same as bytes_transferred, from Boost-docs:
            // "After a successful async_read_until operation, the streambuf may contain additional data beyond the delimiter"
            // The chosen solution is to extract lines from the stream directly when parsing the header. What is left of the
            // streambuf (maybe some bytes of the content) is appended to in the async_read-function below (for retrieving content).
            std::size_t num_additional_bytes = session->request->streambuf.size() - bytes_transferred;

            if(!RequestMessage::parse(session->request->content, session->request->method, session->request->path,
                                      session->request->query_string, session->request->http_version, session->request->header)) {
                if(this->on_error)
                    this->on_error(session->request, make_error_code::make_error_code(errc::protocol_error));
                return;
            }

            // If content, read that as well
            auto header_it = session->request->header.find("Content-Length");
            if(header_it != session->request->header.end()) {
                unsigned long long content_length = 0;
                try {
                    content_length = stoull(header_it->second);
                }
                catch(const std::exception &) {
                    if(this->on_error)
                        this->on_error(session->request, make_error_code::make_error_code(errc::protocol_error));
                    return;
                }
                if(content_length > num_additional_bytes) {
                    session->connection->set_timeout(config.timeout_content);
                    asio::async_read(*session->connection->socket, session->request->streambuf, asio::transfer_exactly(content_length - num_additional_bytes), [this, session](const error_code &ec, std::size_t /*bytes_transferred*/) {
                        session->connection->cancel_timeout();
                        auto lock = session->connection->handler_runner->continue_lock();
                        if(!lock)
                            return;
                        if(!ec) {
                            if(session->request->streambuf.size() == session->request->streambuf.max_size()) {
                                auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                                response->write(StatusCode::client_error_payload_too_large);
                                if(this->on_error)
                                    this->on_error(session->request, make_error_code::make_error_code(errc::message_size));
                                return;
                            }
                            this->find_resource(session);
                        }
                        else if(this->on_error)
                            this->on_error(session->request, ec);
                    });
                }
                else
                    this->find_resource(session);
            }
            else if((header_it = session->request->header.find("Transfer-Encoding")) != session->request->header.end() && header_it->second == "chunked") {
                auto chunks_streambuf = std::make_shared<asio::streambuf>(this->config.max_request_streambuf_size);
                this->read_chunked_transfer_encoded(session, chunks_streambuf);
            }
            else
                this->find_resource(session);
        }
        else if(this->on_error)
            this->on_error(session->request, ec);
    });
}

void ServerBase::read_chunked_transfer_encoded(const std::shared_ptr<Session> &session,
        const std::shared_ptr<asio::streambuf> &chunks_streambuf) {
    session->connection->set_timeout(config.timeout_content);
    asio::async_read_until(*session->connection->socket, session->request->streambuf, "\r\n", [this, session, chunks_streambuf](const error_code &ec, size_t bytes_transferred) {
        session->connection->cancel_timeout();
        auto lock = session->connection->handler_runner->continue_lock();
        if(!lock)
            return;
        if((!ec || ec == asio::error::not_found) && session->request->streambuf.size() == session->request->streambuf.max_size()) {
            auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
            response->write(StatusCode::client_error_payload_too_large);
            if(this->on_error)
                this->on_error(session->request, make_error_code::make_error_code(errc::message_size));
            return;
        }
        if(!ec) {
            std::string line;
            getline(session->request->content, line);
            bytes_transferred -= line.size() + 1;
            line.pop_back();
            unsigned long length = 0;
            try {
                length = stoul(line, 0, 16);
            }
            catch(...) {
                if(this->on_error)
                    this->on_error(session->request, make_error_code::make_error_code(errc::protocol_error));
                return;
            }

            auto num_additional_bytes = session->request->streambuf.size() - bytes_transferred;

            if((2 + length) > num_additional_bytes) {
                session->connection->set_timeout(config.timeout_content);
                asio::async_read(*session->connection->socket, session->request->streambuf, asio::transfer_exactly(2 + length - num_additional_bytes), [this, session, chunks_streambuf, length](const error_code &ec, size_t /*bytes_transferred*/) {
                    session->connection->cancel_timeout();
                    auto lock = session->connection->handler_runner->continue_lock();
                    if(!lock)
                        return;
                    if(!ec) {
                        if(session->request->streambuf.size() == session->request->streambuf.max_size()) {
                            auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                            response->write(StatusCode::client_error_payload_too_large);
                            if(this->on_error)
                                this->on_error(session->request, make_error_code::make_error_code(errc::message_size));
                            return;
                        }
                        this->read_chunked_transfer_encoded_chunk(session, chunks_streambuf, length);
                    }
                    else if(this->on_error)
                        this->on_error(session->request, ec);
                });
            }
            else
                this->read_chunked_transfer_encoded_chunk(session, chunks_streambuf, length);
        }
        else if(this->on_error)
            this->on_error(session->request, ec);
    });
}

void ServerBase::read_chunked_transfer_encoded_chunk(const std::shared_ptr<Session> &session,
        const std::shared_ptr<asio::streambuf> &chunks_streambuf, unsigned long length) {
    std::ostream tmp_stream(chunks_streambuf.get());
    if(length > 0) {
        std::unique_ptr<char[]> buffer(new char[length]);
        session->request->content.read(buffer.get(), static_cast<std::streamsize>(length));
        tmp_stream.write(buffer.get(), static_cast<std::streamsize>(length));
        if(chunks_streambuf->size() == chunks_streambuf->max_size()) {
            auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
            response->write(StatusCode::client_error_payload_too_large);
            if(this->on_error)
                this->on_error(session->request, make_error_code::make_error_code(errc::message_size));
            return;
        }
    }

    // Remove "\r\n"
    session->request->content.get();
    session->request->content.get();

    if(length > 0)
        read_chunked_transfer_encoded(session, chunks_streambuf);
    else {
        if(chunks_streambuf->size() > 0) {
            std::ostream ostream(&session->request->streambuf);
            ostream << chunks_streambuf.get();
        }
        this->find_resource(session);
    }
}

void ServerBase::find_resource(const std::shared_ptr<Session> &session) {
    // Upgrade connection
    if(on_upgrade) {
        auto it = session->request->header.find("Upgrade");
        if(it != session->request->header.end()) {
            // remove connection from connections
            {
                std::lock_guard<std::mutex> lock(*connections_mutex);
                auto it = connections->find(session->connection.get());
                if(it != connections->end())
                    connections->erase(it);
            }

            on_upgrade(session->connection->socket, session->request);
            return;
        }
    }
    // Find path- and method-match, and call write
    for(auto &regex_method : resource) {
        auto it = regex_method.second.find(session->request->method);
        if(it != regex_method.second.end()) {
            regex::smatch sm_res;
            if(regex::regex_match(session->request->path, sm_res, regex_method.first)) {
                session->request->path_match = std::move(sm_res);
                write(session, it->second);
                return;
            }
        }
    }
    auto it = default_resource.find(session->request->method);
    if(it != default_resource.end())
        write(session, it->second);
}

void ServerBase::write(const std::shared_ptr<Session> &session,
           std::function<void(std::shared_ptr<typename ServerBase<socket_type>::Response>,
                   std::shared_ptr<typename ServerBase<socket_type>::Request>)> &resource_function) {
    session->connection->set_timeout(config.timeout_content);
    auto response = std::shared_ptr<Response>(new Response(session, config.timeout_content), [this](Response *response_ptr) {
        auto response = std::shared_ptr<Response>(response_ptr);
        response->send_on_delete([this, response](const error_code &ec) {
            if(!ec) {
                if(response->close_connection_after_response)
                    return;

                auto range = response->session->request->header.equal_range("Connection");
                for(auto it = range.first; it != range.second; it++) {
                    if(case_insensitive_equal(it->second, "close"))
                        return;
                    else if(case_insensitive_equal(it->second, "keep-alive")) {
                        auto new_session = std::make_shared<Session>(this->config.max_request_streambuf_size, response->session->connection);
                        this->read(new_session);
                        return;
                    }
                }
                if(response->session->request->http_version >= "1.1") {
                    auto new_session = std::make_shared<Session>(this->config.max_request_streambuf_size, response->session->connection);
                    this->read(new_session);
                    return;
                }
            }
            else if(this->on_error)
                this->on_error(response->session->request, ec);
        });
    });

    try {
        resource_function(response, session->request);
    }
    catch(const std::exception &) {
        if(on_error)
            on_error(session->request, make_error_code::make_error_code(errc::operation_canceled));
        return;
    }
}