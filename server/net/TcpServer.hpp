#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "../db/Database.hpp"

class TcpServer {
public:
    TcpServer(boost::asio::io_context& io, unsigned short port);

private:
    void accept();

    boost::asio::io_context& io_;
    boost::asio::ip::tcp::acceptor acceptor_;

    boost::asio::ssl::context ssl_ctx_;
    Database db_;
};

