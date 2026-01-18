#pragma once
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "../db/Database.hpp"
#include <array>

class TcpServer {
public:
    TcpServer(boost::asio::io_context& io, unsigned short port);
private:
    void accept();
    void start_udp_discovery();
    boost::asio::io_context& io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::udp::socket udp_sock_;
    boost::asio::ip::udp::endpoint udp_remote_ep_;
    std::array<char, 1024> udp_buf_;
    boost::asio::ssl::context ssl_ctx_;
    Database db_;
};
