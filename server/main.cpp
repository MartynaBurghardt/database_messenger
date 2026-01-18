#include <boost/asio.hpp>
#include <iostream>
#include "net/TcpServer.hpp"
#include <unistd.h>
#include <fstream>

void make_daemon() {
    if (daemon(0, 0) < 0) {
        throw std::runtime_error("Failed to daemonize");
    }
    static std::ofstream log_file("server.log", std::ios::app);
    std::cout.rdbuf(log_file.rdbuf());
    std::cerr.rdbuf(log_file.rdbuf());
}
int main() {
    //make_daemon();
    try {
        boost::asio::io_context io;
        TcpServer server(io, 5555);

        std::cout << "Server listening on port 5555\n";
        io.run();
    } catch (std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }
}

