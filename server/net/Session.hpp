#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "../db/Database.hpp"

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket socket,
            boost::asio::ssl::context& ssl_ctx,
            Database& db);
    ~Session();

    void start();

private:
    void on_handshake(const boost::system::error_code& ec);
    void read_header();
    void read_body(std::size_t length);
    void write_message(const std::vector<char>& msg);

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
    std::array<char, 4> header_{};
    std::vector<char> body_;

    Database& db_;
    std::optional<std::string> logged_user_;

    static std::unordered_map<std::string, Session*> active_sessions_;
};

