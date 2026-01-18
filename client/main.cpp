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
        std::string msg = j.dump();
        uint32_t len = htonl(static_cast<uint32_t>(msg.size()));
        std::vector<boost::asio::const_buffer> buffers{
            boost::asio::buffer(&len, 4),
            boost::asio::buffer(msg)
        };
        boost::asio::async_write(stream_, buffers,
            [](boost::system::error_code ec, std::size_t) {
                if (ec) std::cerr << "Send error: " << ec.message() << "\n";
            });
    }

private:
    void read_header() {
        boost::asio::async_read(stream_, boost::asio::buffer(header_),
            [this](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    uint32_t len;
                    std::memcpy(&len, header_.data(), 4);
                    len = ntohl(len);
                    read_body(len);
                } else {
                    std::cout << "Disconnected from server.\n";
                }
            });
    }

    void read_body(std::size_t len) {
        body_.resize(len);
        boost::asio::async_read(stream_, boost::asio::buffer(body_),
            [this](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    std::string s(body_.begin(), body_.end());
                    try {
                        json res = json::parse(s);
                        if (res["type"] == "stats") {
                            std::cout << "\n[STATYSTYKI]: " << res.value("data", "brak danych") << "\n";
                        } else if (res["type"] == "message") {
                            std::cout << "\n[" << res.value("from", "unknown") << "]: " 
                                      << res.value("message", "") << " (" << res.value("ts", "") << ")\n";
                        } else if (res["type"] == "history") {
                            std::cout << "\n--- HISTORIA WIADOMOSCI ---\n";
                            for (auto& m : res["messages"]) {
                                std::cout << "[" << m.value("ts", "") << "] " 
                                          << m.value("from", "") << " -> " 
                                          << m.value("to", "") << ": " 
                                          << m.value("message", "") << "\n";
                            }
                        } else {
                            std::cout << "\n[SERWER]: " << res.dump() << "\n";
                        }
                    } catch (...) {
                        std::cout << "\n[RAW]: " << s << "\n";
                    }
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

        std::cout << "Zaloguj sie, aby korzystac z komunikatora.\n";
        std::cout << "Komendy: /register <u装 <p>, /login <u装 <p>, /stats, /send <u装 <msg>, /history, /quit\n\n";

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/quit") break;
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            json j;
            if (cmd == "/register" || cmd == "/login") {
                std::string user, pass;
                if (!(iss >> user >> pass)) {
                    std::cout << "Uzycie: " << cmd << " <user> <pass>\n";
                    continue;
                }
                j["type"] = (cmd == "/register" ? "register" : "login");
                j["username"] = user;
                j["password"] = pass;
            } else if (cmd == "/stats") {
                j["type"] = "stats";
            } else if (cmd == "/send") {
                std::string to, msg;
                if (!(iss >> to)) {
                    std::cout << "Uzycie: /send <user> <wiadomosc>\n";
                    continue;
                }
                std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                j["type"] = "send";
                j["to"] = to;
                j["message"] = msg;
            } else if (cmd == "/history") {
                j["type"] = "history";
            } else if (cmd == "/ping") {
                j["type"] = "ping";
            } else {
                std::cout << "Nieznana komenda.\n";
                continue;
            }
            client.send(j);
        }

        io.stop();
        if (net.joinable()) net.join();
    } catch (const std::exception& e) {
        std::cerr << "Blad klienta: " << e.what() << "\n";
    }
    return 0;
}
