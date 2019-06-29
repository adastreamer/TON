#include <webserver/new_http_server.h>

#include <webserver/http_util/session.hpp>
#include <webserver/http_util/responce.hpp>
#include <webserver/http_util/request.hpp>
#include <webserver/http_util/connection.hpp>

namespace SimpleWeb{
    CustomHttpServer::CustomHttpServer(const unsigned short port): config(port),
                                                                   connections(new std::unordered_set<Connection *>()),
                                                                   connections_mutex(new std::mutex()),
                                                                   handler_runner(new ScopeRunner()) {

    }

    CustomHttpServer::~CustomHttpServer() {
        handler_runner->stop();
        stop();
    }

    unsigned short CustomHttpServer::bind() {
        asio::ip::tcp::endpoint endpoint;
        if(config.address.size() > 0)
            endpoint = asio::ip::tcp::endpoint(asio::ip::address::from_string(config.address), config.port);
        else
            endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v6(), config.port);

        if(!io_service) {
            io_service = std::make_shared<asio::io_service>();
            internal_io_service = true;
        }

        if(!acceptor)
            acceptor = std::unique_ptr<asio::ip::tcp::acceptor>(new asio::ip::tcp::acceptor(*io_service));
        acceptor->open(endpoint.protocol());
        acceptor->set_option(asio::socket_base::reuse_address(config.reuse_address));
        if(config.fast_open) {
#if defined(__linux__) && defined(TCP_FASTOPEN)
            const int qlen = 5; // This seems to be the value that is used in other examples.
            error_code ec;
            acceptor->set_option(asio::detail::socket_option::integer<IPPROTO_TCP, TCP_FASTOPEN>(qlen), ec);
#endif // End Linux
        }
        acceptor->bind(endpoint);

        after_bind();

        return acceptor->local_endpoint().port();
    }

    void CustomHttpServer::accept_and_run() {
        acceptor->listen();
        accept();

        if(internal_io_service) {
            if(io_service->stopped())
                io_service->reset();

            // If thread_pool_size>1, start m_io_service.run() in (thread_pool_size-1) threads for thread-pooling
            threads.clear();
            for(std::size_t c = 1; c < config.thread_pool_size; c++) {
                threads.emplace_back([this]() {
                    this->io_service->run();
                });
            }

            // Main thread
            if(config.thread_pool_size > 0)
                io_service->run();

            // Wait for the rest of the threads, if any, to finish as well
            for(auto &t : threads)
                t.join();
        }
    }

    void CustomHttpServer::start() {
        bind();
        accept_and_run();
    }

    void CustomHttpServer::stop() noexcept {
        if(acceptor) {
            error_code ec;
            acceptor->close(ec);

            {
                std::lock_guard<std::mutex> lock(*connections_mutex);
                for(auto &connection : *connections)
                    connection->close();
                connections->clear();
            }

            if(internal_io_service)
                io_service->stop();
        }
    }

    void CustomHttpServer::accept() {
        auto connection = create_connection(*io_service);

        acceptor->async_accept(*connection->socket, [this, connection](const error_code &ec) {
            auto lock = connection->handler_runner->continue_lock();
            if(!lock)
                return;

            // Immediately start accepting a new connection (unless io_service has been stopped)
            if(ec != asio::error::operation_aborted)
                this->accept();

            auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);

            if(!ec) {
                asio::ip::tcp::no_delay option(true);
                error_code ec;
                session->get_connection()->socket->set_option(option, ec);

                this->read(session);
            }
            else if(this->on_error)
                this->on_error(session->get_request(), ec);
        });
    }

    template <typename ... Args>
    std::shared_ptr<Connection> CustomHttpServer::create_connection(Args &&... args) noexcept {
        auto connections = this->connections;
        auto connections_mutex = this->connections_mutex;
        auto connection = std::shared_ptr<Connection>(new Connection(handler_runner, std::forward<Args>(args)...),
                                                      [connections, connections_mutex](Connection *connection) {
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



    void CustomHttpServer::read(const std::shared_ptr<Session> &session) {
        session->get_connection()->set_timeout(config.timeout_request);
        asio::async_read_until(*session->get_connection()->socket, session->get_request()->streambuf,
                "\r\n\r\n", [this, session](const error_code &ec, std::size_t bytes_transferred) {
            session->get_connection()->cancel_timeout();
            auto lock = session->get_connection()->handler_runner->continue_lock();
            if(!lock)
                return;
            session->get_request()->header_read_time = std::chrono::system_clock::now();
            if((!ec || ec == asio::error::not_found) && session->get_request()->streambuf.size() == session->get_request()
                        ->streambuf.max_size()) {
                auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                response->write(StatusCode::client_error_payload_too_large);
                if(this->on_error)
                    this->on_error(session->get_request(), make_error_code::make_error_code(errc::message_size));
                return;
            }
            if(!ec) {
                // request->streambuf.size() is not necessarily the same as bytes_transferred, from Boost-docs:
                // "After a successful async_read_until operation, the streambuf may contain additional data beyond the delimiter"
                // The chosen solution is to extract lines from the stream directly when parsing the header. What is left of the
                // streambuf (maybe some bytes of the content) is appended to in the async_read-function below (for retrieving content).
                std::size_t num_additional_bytes = session->get_request()->streambuf.size() - bytes_transferred;

                if(!RequestMessage::parse(session->get_request()->content, session->get_request()->method,
                        session->get_request()->path,
                        session->get_request()->query_string, session->get_request()->http_version,
                        session->get_request()->header)) {
                    if(this->on_error)
                        this->on_error(session->get_request(), make_error_code::make_error_code(errc::protocol_error));
                    return;
                }

                // If content, read that as well
                auto header_it = session->get_request()->header.find("Content-Length");
                if(header_it != session->get_request()->header.end()) {
                    unsigned long long content_length = 0;
                    try {
                        content_length = stoull(header_it->second);
                    }
                    catch(const std::exception &) {
                        if(this->on_error)
                            this->on_error(session->get_request(), make_error_code::make_error_code(errc::protocol_error));
                        return;
                    }
                    if(content_length > num_additional_bytes) {
                        session->get_connection()->set_timeout(config.timeout_content);
                        asio::async_read(*session->get_connection()->socket, session->get_request()->streambuf,
                                asio::transfer_exactly(content_length - num_additional_bytes), [this, session](const error_code &ec, std::size_t /*bytes_transferred*/) {
                            session->get_connection()->cancel_timeout();
                            auto lock = session->get_connection()->handler_runner->continue_lock();
                            if(!lock)
                                return;
                            if(!ec) {
                                if(session->get_request()->streambuf.size() == session->get_request()->streambuf.max_size()) {
                                    auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                                    response->write(StatusCode::client_error_payload_too_large);
                                    if(this->on_error)
                                        this->on_error(session->get_request(), make_error_code::make_error_code(errc::message_size));
                                    return;
                                }
                                this->find_resource(session);
                            }
                            else if(this->on_error)
                                this->on_error(session->get_request(), ec);
                        });
                    }
                    else
                        this->find_resource(session);
                }
                else if((header_it = session->get_request()->header.find("Transfer-Encoding"))
                        != session->get_request()->header.end() && header_it->second == "chunked") {
                    auto chunks_streambuf = std::make_shared<asio::streambuf>(this->config.max_request_streambuf_size);
                    this->read_chunked_transfer_encoded(session, chunks_streambuf);
                }
                else
                    this->find_resource(session);
            }
            else if(this->on_error)
                this->on_error(session->get_request(), ec);
        });
    }

    void CustomHttpServer::read_chunked_transfer_encoded(const std::shared_ptr<Session> &session,
                                                         const std::shared_ptr<asio::streambuf>& chunks_streambuf) {
        session->get_connection()->set_timeout(config.timeout_content);
        asio::async_read_until(*session->get_connection()->socket, session->get_request()->streambuf, "\r\n",
                [this, session, chunks_streambuf](const error_code &ec, size_t bytes_transferred) {
            session->get_connection()->cancel_timeout();
            auto lock = session->get_connection()->handler_runner->continue_lock();
            if(!lock)
                return;
            if((!ec || ec == asio::error::not_found) && session->get_request()->streambuf.size()
            == session->get_request()->streambuf.max_size()) {
                auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                response->write(StatusCode::client_error_payload_too_large);
                if(this->on_error)
                    this->on_error(session->get_request(), make_error_code::make_error_code(errc::message_size));
                return;
            }
            if(!ec) {
                std::string line;
                getline(session->get_request()->content, line);
                bytes_transferred -= line.size() + 1;
                line.pop_back();
                unsigned long length = 0;
                try {
                    length = stoul(line, 0, 16);
                }
                catch(...) {
                    if(this->on_error)
                        this->on_error(session->get_request(), make_error_code::make_error_code(errc::protocol_error));
                    return;
                }

                auto num_additional_bytes = session->get_request()->streambuf.size() - bytes_transferred;

                if((2 + length) > num_additional_bytes) {
                    session->get_connection()->set_timeout(config.timeout_content);
                    asio::async_read(*session->get_connection()->socket, session->get_request()->streambuf,
                            asio::transfer_exactly(2 + length - num_additional_bytes),
                            [this, session, chunks_streambuf, length](const error_code &ec, size_t /*bytes_transferred*/) {
                        session->get_connection()->cancel_timeout();
                        auto lock = session->get_connection()->handler_runner->continue_lock();
                        if(!lock)
                            return;
                        if(!ec) {
                            if(session->get_request()->streambuf.size() == session->get_request()->streambuf.max_size()) {
                                auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                                response->write(StatusCode::client_error_payload_too_large);
                                if(this->on_error)
                                    this->on_error(session->get_request(), make_error_code::make_error_code(errc::message_size));
                                return;
                            }
                            this->read_chunked_transfer_encoded_chunk(session, chunks_streambuf, length);
                        }
                        else if(this->on_error)
                            this->on_error(session->get_request(), ec);
                    });
                }
                else
                    this->read_chunked_transfer_encoded_chunk(session, chunks_streambuf, length);
            }
            else if(this->on_error)
                this->on_error(session->get_request(), ec);
        });
    }

    void CustomHttpServer::read_chunked_transfer_encoded_chunk(const std::shared_ptr<Session> &session,
                                                               const std::shared_ptr<asio::streambuf>& chunks_streambuf,
                                                               unsigned long length) {
        std::ostream tmp_stream(chunks_streambuf.get());
        if(length > 0) {
            std::unique_ptr<char[]> buffer(new char[length]);
            session->get_request()->content.read(buffer.get(), static_cast<std::streamsize>(length));
            tmp_stream.write(buffer.get(), static_cast<std::streamsize>(length));
            if(chunks_streambuf->size() == chunks_streambuf->max_size()) {
                auto response = std::shared_ptr<Response>(new Response(session, this->config.timeout_content));
                response->write(StatusCode::client_error_payload_too_large);
                if(this->on_error)
                    this->on_error(session->get_request(), make_error_code::make_error_code(errc::message_size));
                return;
            }
        }

        if(length > 0)
            read_chunked_transfer_encoded(session, chunks_streambuf);
        else {
            if(chunks_streambuf->size() > 0) {
                std::ostream ostream(&session->get_request()->streambuf);
                ostream << chunks_streambuf.get();
            }
            this->find_resource(session);
        }
    }

    void CustomHttpServer::find_resource(const std::shared_ptr<Session> &session) {
        // Upgrade connection
        if(on_upgrade) {
            auto it = session->get_request()->header.find("Upgrade");
            if(it != session->get_request()->header.end()) {
                // remove connection from connections
                {
                    std::lock_guard<std::mutex> lock(*connections_mutex);
                    auto it = connections->find(session->get_connection().get());
                    if(it != connections->end())
                        connections->erase(it);
                }

                on_upgrade(session->get_connection()->socket, session->get_request());
                return;
            }
        }
        // Find path- and method-match, and call write
        for(auto &regex_method : resource) {
            auto it = regex_method.second.find(session->get_request()->method);
            if(it != regex_method.second.end()) {
                regex::smatch sm_res;
                if(regex::regex_match(session->get_request()->path, sm_res, regex_method.first)) {
                    session->get_request()->path_match = std::move(sm_res);
                    write(session, it->second);
                    return;
                }
            }
        }
        auto it = default_resource.find(session->get_request()->method);
        if(it != default_resource.end())
            write(session, it->second);
    }

    void CustomHttpServer::write(const std::shared_ptr<Session> &session, std::function<void(std::shared_ptr<Response>,
                                                                                             std::shared_ptr<Request>)> &
    resource_function) {
        session->get_connection()->set_timeout(config.timeout_content);
        auto response = std::shared_ptr<Response>(new Response(session, config.timeout_content),
                                                  [this](Response* response_ptr) {
            auto response = std::shared_ptr<Response>(response_ptr);
            response->send_on_delete([this, response](const error_code &ec) {
                if(!ec) {
                    if(response->close_connection_after_response)
                        return;

                    auto range = response->getSession()->get_request()->header.equal_range("Connection");
                    for(auto it = range.first; it != range.second; it++) {
                        if(case_insensitive_equal(it->second, "close"))
                            return;
                        else if(case_insensitive_equal(it->second, "keep-alive")) {
                            auto new_session = std::make_shared<Session>(this->config.max_request_streambuf_size,
                                    response->getSession()->get_connection());
                            this->read(new_session);
                            return;
                        }
                    }
                    if(response->getSession()->get_request()->http_version >= "1.1") {
                        auto new_session = std::make_shared<Session>(this->config.max_request_streambuf_size,
                                response->getSession()->get_connection());
                        this->read(new_session);
                        return;
                    }
                }
                else if(this->on_error)
                    this->on_error(response->getSession()->get_request(), ec);
            });
        });

        try {
            resource_function(response, session->get_request());
        }
        catch(const std::exception &) {
            if(on_error)
                on_error(session->get_request(), make_error_code::make_error_code(errc::operation_canceled));
            return;
        }
    }
}

