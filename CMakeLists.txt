cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0167 NEW)

project(chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(server
    server/main.cpp
    server/net/TcpServer.cpp
    server/net/Session.cpp
    server/db/Database.cpp
)

target_link_libraries(server
    Boost::headers
    OpenSSL::Crypto
    SQLite::SQLite3
    OpenSSL::SSL
)

add_executable(client
    client/main.cpp
)

target_link_libraries(client
    Boost::headers
    OpenSSL::SSL
    OpenSSL::Crypto
)

