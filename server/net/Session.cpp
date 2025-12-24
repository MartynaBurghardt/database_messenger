#include "Session.hpp"
#include <boost/asio.hpp>
#include <cstring>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

using boost::asio::ip::tcp;
using json = nlohmann::json;
namespace ssl = boost::asio::ssl;

std::unordered_map<std::string, Session*> Session::active_sessions_;
static std::vector<unsigned char> random_bytes(std::size_t n) {
    std::vector<unsigned char> out(n);
    RAND_bytes(out.data(), static_cast<int>(out.size()));
    return out;
}
static std::vector<unsigned char> pbkdf2_sha256(
    const std::string& password,
    const std::vector<unsigned char>& salt,
    int iterations = 120000,
    std::size_t dk_len = 32
) {
    std::vector<unsigned char> out(dk_len);
    PKCS5_PBKDF2_HMAC(
        password.c_str(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        iterations,
        EVP_sha256(),
        static_cast<int>(dk_len),
        out.data()
    );
    return out;
}

static bool constant_time_equal(
    const std::vector<unsigned char>& a,
    const std::vector<unsigned char>& b
) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
Session::Session(tcp::socket socket, ssl::context& ssl_ctx, Database& db)
    : stream_(std::move(socket), ssl_ctx), db_(db) {}

void Session::start() {
auto self = shared_from_this();
stream_.async_handshake(ssl::stream_base::server,
[this, self](const boost::system::error_code& ec) {
on_handshake(ec);
});
}
void Session::on_handshake(const boost::system::error_code& ec) {
if (!ec) read_header();
}

Session::~Session() {
    if (logged_user_) {
        active_sessions_.erase(*logged_user_);
    }
}

void Session::read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(
        stream_,
        boost::asio::buffer(header_),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                uint32_t length = 0;
                std::memcpy(&length, header_.data(), 4);
                length = ntohl(length);
                read_body(length);
            }
        }
    );
}

void Session::read_body(std::size_t length) {
    auto self = shared_from_this();
    body_.resize(length);

    boost::asio::async_read(
        stream_,
        boost::asio::buffer(body_),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                std::string text(body_.begin(), body_.end());
                json response;

                try {
                    json req = json::parse(text);
                    std::string type = req.value("type", "");

                    if (type != "login" && type != "register" && !logged_user_) {
                        response["type"] = "error";
                        response["message"] = "not authenticated";
                    }
                    else if (type == "ping") {
                        response["type"] = "pong";
                    }
                    else if (type == "register") {
                        std::string user = req.value("username", "");
                        std::string pass = req.value("password", "");

                        if (user.empty() || pass.empty()) {
                            response["type"] = "error";
                            response["message"] = "missing fields";
                        }
                        else if (db_.get_user(user)) {
                            response["type"] = "error";
                            response["message"] = "user exists";
                        }
                        else {
                            auto salt = random_bytes(16);
                            auto hash = pbkdf2_sha256(pass, salt);
                            db_.create_user(user, salt, hash);
                            logged_user_.reset();
                            response["type"] = "ok";
                        }
                    }
                    else if (type == "login") {
                        std::string user = req.value("username", "");
                        std::string pass = req.value("password", "");

                        auto rec = db_.get_user(user);
                        if (!rec) {
                            response["type"] = "error";
                            response["message"] = "no such user";
                        }
                        else {
                            auto computed = pbkdf2_sha256(pass, rec->salt);
                            if (!constant_time_equal(computed, rec->hash)) {
                                response["type"] = "error";
                                response["message"] = "wrong password";
                            }
                            else {
                                logged_user_ = user;
                                active_sessions_[user] = this;
                                auto pending = db_.get_undelivered(user);
                                for (auto& m : pending) {
                                    json msg;
                                    msg["type"] = "message";
                                    msg["from"] = m.from;
                                    msg["message"] = m.content;
                                    msg["ts"] = m.ts;
                                    std::string out = msg.dump();
                                    std::vector<char> data(out.begin(), out.end());
                                    write_message(data);
                             }
 
                            db_.mark_delivered(user);
                            response["type"] = "ok";
                            }
                        }
                    }
                    else if (type == "send") {
                        std::string to = req.value("to", "");
                        std::string content = req.value("message", "");

                        if (to.empty() || content.empty()) {
                            response["type"] = "error";
                            response["message"] = "missing fields";
                        }
                        else {
                            std::string from = *logged_user_;

                            db_.save_message(from, to, content);

                            auto it = active_sessions_.find(to);
                            if (it != active_sessions_.end()) {
                                json msg;
                                msg["type"] = "message";
                                msg["from"] = from;
                                msg["message"] = content;

                                std::string out = msg.dump();
                                std::vector<char> data(out.begin(), out.end());
                                it->second->write_message(data);
                            }

                            response["type"] = "ok";
                        }
                    }
                    else if (type == "history") {
                        auto rows = db_.get_history(*logged_user_);
                        json arr = json::array();
                        for (auto& m : rows) {
                        arr.push_back({
                        {"from", m.from},
                        {"to", m.to},
                        {"message", m.content},
                        {"ts", m.ts}
                        });
                       }
 
                     response["type"] = "history";
                    response["messages"] = arr;
                    }

                    else {
                        response["type"] = "error";
                        response["message"] = "unknown command";
                    }
                }
                catch (...) {
                    response["type"] = "error";
                    response["message"] = "invalid json";
                }

                std::string out = response.dump();
                std::vector<char> data(out.begin(), out.end());
                write_message(data);
                read_header();
            }
        }
    );
}

void Session::write_message(const std::vector<char>& msg) {
    auto self = shared_from_this();
    uint32_t len = htonl(static_cast<uint32_t>(msg.size()));
    std::vector<boost::asio::const_buffer> buffers{ boost::asio::buffer(&len, 4), boost::asio::buffer(msg) };
    boost::asio::async_write( stream_, buffers,[this, self](boost::system::error_code, std::size_t) {}  );
}

