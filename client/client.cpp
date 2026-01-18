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
        : ssl_ctx_(ssl::context::tls_client), stream_(io, ssl_ctx_) {
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
        std::vector<boost::asio::const_buffer> buffers{ boost::asio::buffer(&len, 4), boost::asio::buffer(msg) };
        boost::asio::async_write(stream_, buffers, [](boost::system::error_code ec, std::size_t) {});
    }

private:
    void read_header() {
        boost::asio::async_read(stream_, boost::asio::buffer(header_), [this](boost::system::error_code ec, std::size_t) {
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
        boost::asio::async_read(stream_, boost::asio::buffer(body_), [this](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                std::string s(body_.begin(), body_.end());
                try {
                    json res = json::parse(s);
                    std::string type = res.value("type", "");

                    if (type == "message") {
                        std::cout << "\n\033[1;32m[" << res.value("from", "System") << "]\033[0m: " << res.value("message", "") << std::endl;
                    } 
                    else if (type == "stats") {
                        std::cout << "\n\033[1;34m╔════════ STATISTICS ════════╗\033[0m\n " << res.value("data", "") << "\n\033[1;34m╚════════════════════════════╝\033[0m" << std::endl;
                    } 
                    else if (type == "group_members") {
                        std::cout << "\n\033[1;33m Members of " << res.value("group", "") << ":\033[0m";
                        for (auto& m : res["members"]) std::cout << " " << m.get<std::string>() << ",";
                        std::cout << std::endl;
                    }
                    else if (type == "history") {
                        std::cout << "\n\033[1;36m--- MESSAGE HISTORY ---\033[0m" << std::endl;
                        for (auto& m : res["messages"]) {
                            std::cout << "[" << m.value("ts", "") << "] " << m.value("from", "") << " -> " << m.value("to", "") << ": " << m.value("message", "") << std::endl;
                        }
                    }
                    else if (type == "error") {
                        std::cout << "\n\033[1;31m[!] ERROR:\033[0m " << res.value("message", "") << std::endl;
                    }
                    else {
                        std::cout << "\n\033[1;30m[SERVER]:\033[0m " << res.value("message", "OK") << std::endl;
                    }
                } catch (...) {}
                std::cout << "\033[1;37m>\033[0m " << std::flush;
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

        std::cout << "\033[1;35m╔════════════════════════════════════════╗\033[0m" << std::endl;
        std::cout << "\033[1;35m║          DISTRIBUTED MESSENGER         ║\033[0m" << std::endl;
        std::cout << "\033[1;35m╠════════════════════════════════════════╣\033[0m" << std::endl;
        std::cout << "\033[1;37m║ /register <user <p> | /login <user <p> ║\033[0m" << std::endl;
        std::cout << "\033[1;37m║ /send <user <msg>   | /stats           ║\033[0m" << std::endl;
        std::cout << "\033[1;37m║ /create_group <g>   | /join <g>        ║\033[0m" << std::endl;
        std::cout << "\033[1;37m║ /send_group <g> <m> | /members <g>     ║\033[0m" << std::endl;
        std::cout << "\033[1;37m║ /history            | /quit            ║\033[0m" << std::endl;
        std::cout << "\033[1;35m╚════════════════════════════════════════╝\033[0m" << std::endl;

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/quit") break;
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string cmd; iss >> cmd;
            json j;
            if (cmd == "/register" || cmd == "/login") {
                std::string u, p; if(!(iss >> u >> p)) continue;
                j["type"] = (cmd == "/register" ? "register" : "login");
                j["username"] = u; j["password"] = p;
            } else if (cmd == "/stats") j["type"] = "stats";
            else if (cmd == "/create_group") { std::string g; iss >> g; j["type"] = "create_group"; j["group"] = g; }
            else if (cmd == "/join") { std::string g; iss >> g; j["type"] = "join_group"; j["group"] = g; }
            else if (cmd == "/members") { std::string g; iss >> g; j["type"] = "group_members"; j["group"] = g; }
            else if (cmd == "/send") {
                std::string to, msg; iss >> to; std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                j["type"] = "send"; j["to"] = to; j["message"] = msg;
            } else if (cmd == "/send_group") {
                std::string g, msg; iss >> g; std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                j["type"] = "send_group"; j["group"] = g; j["message"] = msg;
            } else if (cmd == "/history") j["type"] = "history";
            else continue;
            client.send(j);
        }
        io.stop(); net.join();
    } catch (const std::exception& e) { std::cerr << e.what() << std::endl; }
    return 0;
}
