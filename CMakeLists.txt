cmake_minimum_required(VERSION 3.10)
project(ChatProject)

set(CMAKE_CXX_STANDARD 20)

# Szukanie pakietów
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# Wyświetl debug (opcjonalne, pomoże sprawdzić ścieżki)
message(STATUS "SQLite3 include: ${SQLITE3_INCLUDE_DIRS}")
message(STATUS "SQLite3 libs: ${SQLITE3_LIBRARIES}")

include_directories(${OPENSSL_INCLUDE_DIR} ${SQLITE3_INCLUDE_DIRS})

# Serwer
add_executable(server 
    server/main.cpp 
    server/net/Tcpserver.cpp 
    server/net/Session.cpp 
    server/db/Database.cpp
)
# Dodaliśmy bezpośrednią zmienną SQLite3_LIBRARIES
target_link_libraries(server 
    ${OPENSSL_LIBRARIES} 
    ${SQLITE3_LIBRARIES} 
    sqlite3 
    Threads::Threads
)

# Klient
add_executable(client client/client.cpp)
target_link_libraries(client 
    ${OPENSSL_LIBRARIES} 
    Threads::Threads
)
