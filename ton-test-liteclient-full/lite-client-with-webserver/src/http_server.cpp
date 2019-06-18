#include <webserver/http_server.hpp>


namespace SimpleWeb{

    CustomHttpServer::CustomHttpServer(const unsigned short port) {

    }

    CustomHttpServer::~CustomHttpServer() {

    }

    unsigned short CustomHttpServer::bind() {
        return 0;
    }

    void CustomHttpServer::accept_and_run() {

    }

    void CustomHttpServer::start() {

    }

    void CustomHttpServer::stop() noexcept {

    }

    void CustomHttpServer::accept() {

    }

    void CustomHttpServer::read(const std::shared_ptr<Session> &session) {

    }

    void CustomHttpServer::read_chunked_transfer_encoded(const std::shared_ptr<Session> &session,
                                                         const std::shared_ptr<asio::streambuf>& chunks_streambuf) {

    }

    void CustomHttpServer::read_chunked_transfer_encoded_chunk(const std::shared_ptr<Session> &session,
                                                               const std::shared_ptr<asio::streambuf>& chunks_streambuf,
                                                               unsigned long length) {

    }

    void CustomHttpServer::find_resource(const std::shared_ptr<Session> &session) {

    }

    void CustomHttpServer::write(const std::shared_ptr<Session> &session, std::function<void(std::shared_ptr<Responce>,
                                                                                             std::shared_ptr<Request>)>&
                                                                                             resource_function) {

    }
}

