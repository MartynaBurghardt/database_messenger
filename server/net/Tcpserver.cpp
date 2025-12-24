#include "TcpServer.hpp"
#include "Session.hpp"

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

TcpServer::TcpServer(boost::asio::io_context& io, unsigned short port)
    : io_(io),
      acceptor_(io, tcp::endpoint(tcp::v4(), port)),
      ssl_ctx_(ssl::context::tls_server),
      db_("chat.db")
{
    ssl_ctx_.set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::no_sslv3
    );

    ssl_ctx_.use_certificate_chain_file("certs/server.crt");
    ssl_ctx_.use_private_key_file("certs/server.key", ssl::context::pem);

    accept();
}

void TcpServer::accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(std::move(socket), ssl_ctx_, db_)->start();
            }
            accept();
        }
    );
}

