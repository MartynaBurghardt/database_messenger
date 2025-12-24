#include "Database.hpp"
#include <stdexcept>
#include <algorithm>

Database::Database(const std::string& path) : db_(nullptr) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("cannot open database");
    }

    const char* users_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "salt BLOB NOT NULL,"
        "hash BLOB NOT NULL"
        ");";

    sqlite3_exec(db_, users_sql, nullptr, nullptr, nullptr);

    const char* messages_sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT NOT NULL,"
        "receiver TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "ts DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "delivered INTEGER DEFAULT 0"
        ");";

    sqlite3_exec(db_, messages_sql, nullptr, nullptr, nullptr);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

bool Database::create_user(const std::string& username,
                           const std::vector<unsigned char>& salt,
                           const std::vector<unsigned char>& hash) {
    const char* sql =
        "INSERT INTO users (username, salt, hash) VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, salt.data(), (int)salt.size(), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, hash.data(), (int)hash.size(), SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

void Database::save_message(const std::string& from,
                            const std::string& to,
                            const std::string& content) {
    const char* sql =
        "INSERT INTO messages (sender, receiver, content) VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<UserRecord> Database::get_user(const std::string& username) {
    const char* sql =
        "SELECT salt, hash FROM users WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return std::nullopt;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    UserRecord rec;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* salt =
            (const unsigned char*)sqlite3_column_blob(stmt, 0);
        int salt_len = sqlite3_column_bytes(stmt, 0);

        const unsigned char* hash =
            (const unsigned char*)sqlite3_column_blob(stmt, 1);
        int hash_len = sqlite3_column_bytes(stmt, 1);

        rec.salt.assign(salt, salt + salt_len);
        rec.hash.assign(hash, hash + hash_len);

        sqlite3_finalize(stmt);
        return rec;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<MessageRecord> Database::get_history(const std::string& user, int limit) {
    const char* sql =
        "SELECT sender, receiver, content, ts "
        "FROM messages "
        "WHERE sender = ? OR receiver = ? "
        "ORDER BY ts DESC "
        "LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);

    std::vector<MessageRecord> out;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageRecord m;
        m.from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.to = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        m.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out.push_back(m);
    }

    sqlite3_finalize(stmt);
    std::reverse(out.begin(), out.end());
    return out;
}
std::vector<MessageRecord> Database::get_undelivered(const std::string& user) {
    const char* sql =
        "SELECT sender, receiver, content, ts "
        "FROM messages "
        "WHERE receiver = ? AND delivered = 0 "
        "ORDER BY ts;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<MessageRecord> out;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageRecord m;
        m.from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.to = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        m.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out.push_back(m);
    }

    sqlite3_finalize(stmt);
    return out;
}

void Database::mark_delivered(const std::string& user) {
    const char* sql =
        "UPDATE messages SET delivered = 1 WHERE receiver = ?;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

