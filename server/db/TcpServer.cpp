#include "TcpServer.hpp"
#include "Session.hpp"
#include <iostream>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;
namespace ssl = boost::asio::ssl;

TcpServer::TcpServer(boost::asio::io_context& io, unsigned short port)
    : io_(io),
      acceptor_(io, tcp::endpoint(tcp::v4(), port)),
      udp_sock_(io, udp::endpoint(udp::v4(), 8888)),
      ssl_ctx_(ssl::context::tls_server),
      db_("chat.db")
{
    ssl_ctx_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3);
    ssl_ctx_.use_certificate_chain_file("certs/server.crt");
    ssl_ctx_.use_private_key_file("certs/server.key", ssl::context::pem);

    boost::asio::ip::address_v4 mcast = boost::asio::ip::address_v4::from_string("239.255.0.1");
    udp_sock_.set_option(boost::asio::ip::multicast::join_group(mcast));

    accept();
    start_udp_discovery();
}

void TcpServer::start_udp_discovery() {
    udp_sock_.async_receive_from(boost::asio::buffer(udp_buf_), udp_remote_ep_,
        [this](boost::system::error_code ec, std::size_t bytes) {
            if (!ec) {
                std::string msg(udp_buf_.data(), bytes);
                if (msg == "DISCOVER_SERVER") {
                    std::string resp = "I_AM_SERVER";
                    udp_sock_.send_to(boost::asio::buffer(resp), udp_remote_ep_);
                }
            }
            start_udp_discovery();
        });
}

void TcpServer::accept() {
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) std::make_shared<Session>(std::move(socket), ssl_ctx_, db_)->start();
        accept();
    });
}
