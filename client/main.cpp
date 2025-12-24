#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <array>
#include <cstring>
#include <sstream>
#include <nlohmann/json.hpp>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using json = nlohmann::json;

class Client {
public:
    explicit Client(boost::asio::io_context& io)
        : ssl_ctx_(ssl::context::tls_client),
          stream_(io, ssl_ctx_) {
        ssl_ctx_.set_verify_mode(ssl::verify_none);
    }

    void connect(const std::string& host, const std::string& port) {
        tcp::resolver resolver(stream_.get_executor());
        auto endpoints = resolver.resolve(host, port);
        boost::asio::connect(stream_.next_layer(), endpoints);
        stream_.handshake(ssl::stream_base::client);
        read_header();
    }

    void send(const json& j) {
        write(j.dump());
    }

private:
    void write(const std::string& msg) {
        uint32_t len = htonl(static_cast<uint32_t>(msg.size()));
        std::vector<boost::asio::const_buffer> buffers{
            boost::asio::buffer(&len, 4),
            boost::asio::buffer(msg)
        };
        boost::asio::async_write(stream_, buffers,
            [](boost::system::error_code, std::size_t) {});
    }

    void read_header() {
        boost::asio::async_read(stream_, boost::asio::buffer(header_),
            [this](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    uint32_t len;
                    std::memcpy(&len, header_.data(), 4);
                    len = ntohl(len);
                    read_body(len);
                }
            });
    }

    void read_body(std::size_t len) {
        body_.resize(len);
        boost::asio::async_read(stream_, boost::asio::buffer(body_),
            [this](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    std::cout << "< " << std::string(body_.begin(), body_.end()) << "\n";
                    read_header();
                }
            });
    }

    ssl::context ssl_ctx_;
    ssl::stream<tcp::socket> stream_;
    std::array<char, 4> header_{};
    std::vector<char> body_;
};

int main() {
    try {
        boost::asio::io_context io;
        Client client(io);
        client.connect("127.0.0.1", "5555");

        std::thread net([&]() { io.run(); });

        std::cout << "Commands:\n";
        std::cout << "/register user pass\n";
        std::cout << "/login user pass\n";
        std::cout << "/ping\n";
        std::cout << "/send user message\n";
        std::cout << "/history\n";
        std::cout << "/quit\n\n";

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/quit") break;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            json j;

            if (cmd == "/register") {
                std::string user, pass;
                iss >> user >> pass;
                j["type"] = "register";
                j["username"] = user;
                j["password"] = pass;
            } else if (cmd == "/login") {
                std::string user, pass;
                iss >> user >> pass;
                j["type"] = "login";
                j["username"] = user;
                j["password"] = pass;
            } else if (cmd == "/ping") {
                j["type"] = "ping";
            } else if (cmd == "/send") {
                std::string to;
                iss >> to;
                std::string msg;
                std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                j["type"] = "send";
                j["to"] = to;
                j["message"] = msg;
            } else if (cmd == "/history") {
                j["type"] = "history";
            } else {
                std::cout << "Unknown command\n";
                continue;
            }

            client.send(j);
        }

        io.stop();
        net.join();
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << "\n";
    }
}

