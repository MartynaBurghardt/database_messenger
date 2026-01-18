sudo pkill server; sudo pkill client; rm -f build/chat.db chat.db
cd build
cmake .. && make -j4
cp -r ../certs . && ./server    ------w pierwszym oknie terminala bedąc w build
./client
