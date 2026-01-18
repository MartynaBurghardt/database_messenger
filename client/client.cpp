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
using boost::asio::ip::udp;
namespace ssl = boost::asio::ssl;
using json = nlohmann::json;

std::string discover_server(boost::asio::io_context& io) {
    udp::socket sock(io, udp::v4());
    sock.set_option(boost::asio::socket_base::broadcast(true));
    udp::endpoint mcast_ep(boost::asio::ip::make_address_v4("239.255.0.1"), 8888);
    std::string msg = "DISCOVER_SERVER";
    sock.send_to(boost::asio::buffer(msg), mcast_ep);
    char buf[1024];
    udp::endpoint sender_ep;
    sock.receive_from(boost::asio::buffer(buf), sender_ep);
    return sender_ep.address().to_string();
}

class Client {
public:
    explicit Client(boost::asio::io_context& io) : ssl_ctx_(ssl::context::tls_client), stream_(io, ssl_ctx_) {
        ssl_ctx_.set_verify_mode(ssl::verify_none);
    }

    void connect(const std::string& host, const std::string& port) {
        tcp::resolver resolver(stream_.get_executor());
        boost::asio::connect(stream_.next_layer(), resolver.resolve(host, port));
        stream_.handshake(ssl::stream_base::client);
        read_header();
    }

    void send(const json& j) {
        std::string msg = j.dump();
        uint32_t len = htonl(static_cast<uint32_t>(msg.size()));
        std::vector<boost::asio::const_buffer> bufs{ boost::asio::buffer(&len, 4), boost::asio::buffer(msg) };
        boost::asio::async_write(stream_, bufs, [](boost::system::error_code, std::size_t) {});
    }

private:
    void read_header() {
        boost::asio::async_read(stream_, boost::asio::buffer(header_), [this](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                uint32_t len; std::memcpy(&len, header_.data(), 4);
                read_body(ntohl(len));
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
                    std::string t = res.value("type", "");

                    if (t == "message") {
                        std::cout << "\n\033[1;32m[" << res.value("from", "System") << "]\033[0m: " << res.value("message", "") << "\n";
                    } 
                    else if (t == "stats") {
                        std::cout << "\n\033[1;34m╔════════ STATYSTYKI ════════╗\033[0m\n " << res.value("data", "") << "\n\033[1;34m╚════════════════════════════╝\033[0m\n";
                    } 
                    else if (t == "group_members") {
                        std::cout << "\n\033[1;33m Członkowie grupy " << res.value("group", "") << ":\033[0m ";
                        for(auto& m : res["members"]) std::cout << m.get<std::string>() << " ";
                        std::cout << "\n";
                    }
                    else if (t == "history") {
                        std::cout << "\n\033[1;36m--- HISTORIA WIADOMOŚCI ---\033[0m\n";
                        for (auto& m : res["messages"]) {
                            std::cout << "[" << m.value("ts", "") << "] " << m.value("from", "") << " -> " << m.value("to", "") << ": " << m.value("message", "") << "\n";
                        }
                    }
                    else if (t == "ok") {
                        std::cout << "\n\033[1;32m[OK]:\033[0m " << res.value("message", "Operacja powiodła się") << "\n";
                    }
                    else if (t == "error") {
                        std::cout << "\n\033[1;31m[BŁĄD]:\033[0m " << res.value("message", "Nieznany błąd") << "\n";
                    }
                    else {
                        std::cout << "\n\033[1;30m[SERWER]:\033[0m " << res.dump() << "\n";
                    }
                } catch(...) {}
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
        std::cout << "\033[1;33mSzukanie serwera przez Multicast...\033[0m\n";
        std::string ip = discover_server(io);
        std::cout << "\033[1;32mZnaleziono serwer:\033[0m " << ip << "\n";

        Client c(io);
        c.connect(ip, "5555");
        std::thread n([&]() { io.run(); });

        std::cout << "\033[1;35m"
                  << "╔════════════════════════════════════════╗\n"
                  << "║          DISTRIBUTED MESSENGER         ║\n"
                  << "╠════════════════════════════════════════╣\n"
                  << "║ /register <u> <p>  |  /login <u> <p>   ║\n"
                  << "║ /send <u> <msg>    |  /stats           ║\n"
                  << "║ /create_group <g>  |  /join <g>        ║\n"
                  << "║ /send_group <g> <m>|  /members <g>     ║\n"
                  << "║ /history           |  /quit            ║\n"
                  << "╚════════════════════════════════════════╝\n\033[0m";

        std::string l;
        while (std::getline(std::cin, l)) {
            if (l == "/quit") break;
            if (l.empty()) continue;
            std::istringstream iss(l); std::string cmd; iss >> cmd;
            json j;
            if (cmd == "/register" || cmd == "/login") {
                std::string u, p; if(!(iss >> u >> p)) continue;
                j["type"] = (cmd == "/register" ? "register" : "login");
                j["username"] = u; j["password"] = p;
            } else if (cmd == "/send") {
                std::string to, m; iss >> to; std::getline(iss, m);
                if(!m.empty() && m[0]==' ') m.erase(0,1);
                j["type"] = "send"; j["to"] = to; j["message"] = m;
            } else if (cmd == "/send_group") {
                std::string g, m; iss >> g; std::getline(iss, m);
                if(!m.empty() && m[0]==' ') m.erase(0,1);
                j["type"] = "send_group"; j["group"] = g; j["message"] = m;
            } else if (cmd == "/create_group") { std::string g; if(!(iss >> g)) continue; j["type"] = "create_group"; j["group"] = g; }
            else if (cmd == "/join") { std::string g; if(!(iss >> g)) continue; j["type"] = "join_group"; j["group"] = g; }
            else if (cmd == "/members") { std::string g; if(!(iss >> g)) continue; j["type"] = "group_members"; j["group"] = g; }
            else if (cmd == "/stats") j["type"] = "stats";
            else if (cmd == "/history") j["type"] = "history";
            else continue;
            c.send(j);
        }
        io.stop(); n.join();
    } catch (std::exception& e) { std::cerr << "\n\033[1;31mBłąd:\033[0m " << e.what() << "\n"; }
    return 0;
}
