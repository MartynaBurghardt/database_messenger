#include <boost/asio.hpp>
#include <iostream>
#include "net/TcpServer.hpp"

int main() {
    try {
        boost::asio::io_context io;
        TcpServer server(io, 5555);

        std::cout << "Server listening on port 5555\n";
        io.run();
    } catch (std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }
}

