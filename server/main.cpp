#include <boost/asio.hpp>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include "net/TcpServer.hpp"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

void daemonize() {
    if (daemon(0, 0) < 0) exit(1);
    static std::ofstream log("server.log", std::ios::app);
    std::cout.rdbuf(log.rdbuf());
    std::cerr.rdbuf(log.rdbuf());
}

int main() {
    try {
        // daemonize(); 
        boost::asio::io_context io;
        TcpServer server(io, 5555);
        std::cout << "Server started on port 5555\n";
        io.run();
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}
