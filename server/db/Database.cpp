#include "Database.hpp"
#include <stdexcept>
#include <algorithm>

Database::Database(const std::string& path) : db_(nullptr) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("cannot open database");
    }

    const char* sql = 
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "salt BLOB NOT NULL,"
        "hash BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT NOT NULL,"
        "receiver TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "ts DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "delivered INTEGER DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS groups ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS group_members ("
        "group_id INTEGER, user_id INTEGER,"
        "FOREIGN KEY(group_id) REFERENCES groups(id), "
        "FOREIGN KEY(user_id) REFERENCES users(id)"
        ");"
        "CREATE VIEW IF NOT EXISTS v_user_stats AS "
        "SELECT u.username, "
        "(SELECT COUNT(*) FROM messages WHERE sender = u.username) as sent_count, "
        "(SELECT MAX(ts) FROM messages WHERE sender = u.username) as last_sent "
        "FROM users u;"
        "CREATE TRIGGER IF NOT EXISTS trg_prevent_self_msg "
        "BEFORE INSERT ON messages "
        "WHEN NEW.sender = NEW.receiver "
        "BEGIN "
        "SELECT RAISE(ABORT, 'Cannot send message to yourself'); "
        "END;";

    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

bool Database::create_user(const std::string& username,
                           const std::vector<unsigned char>& salt,
                           const std::vector<unsigned char>& hash) {
    const char* sql = "INSERT INTO users (username, salt, hash) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, salt.data(), (int)salt.size(), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, hash.data(), (int)hash.size(), SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::save_message(const std::string& from, const std::string& to, const std::string& content) {
    const char* sql = "INSERT INTO messages (sender, receiver, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::create_group(const std::string& group_name) {
    const char* sql = "INSERT INTO groups (name) VALUES (?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, group_name.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

void Database::add_to_group(const std::string& group_name, const std::string& username) {
    const char* sql = 
        "INSERT INTO group_members (group_id, user_id) "
        "SELECT g.id, u.id FROM groups g, users u "
        "WHERE g.name = ? AND u.username = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, group_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<UserRecord> Database::get_user(const std::string& username) {
    const char* sql = "SELECT salt, hash FROM users WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    UserRecord rec;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* salt = (const unsigned char*)sqlite3_column_blob(stmt, 0);
        int salt_len = sqlite3_column_bytes(stmt, 0);
        const unsigned char* hash = (const unsigned char*)sqlite3_column_blob(stmt, 1);
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
    const char* sql = "SELECT sender, receiver, content, ts FROM messages WHERE sender = ? OR receiver = ? ORDER BY ts DESC LIMIT ?;";
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
    const char* sql = "SELECT sender, receiver, content, ts FROM messages WHERE receiver = ? AND delivered = 0 ORDER BY ts;";
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
    const char* sql = "UPDATE messages SET delivered = 1 WHERE receiver = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string Database::get_stats(const std::string& username) {
    const char* sql = "SELECT sent_count, last_sent FROM v_user_stats WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    std::string result = "No stats";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            const char* last = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            result = "Sent: " + std::to_string(count) + ", Last: " + (last ? last : "never");
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> Database::get_group_members(const std::string& group_name) {
    const char* sql = "SELECT u.username FROM users u JOIN group_members gm ON u.id = gm.user_id JOIN groups g ON g.id = gm.group_id WHERE g.name = ?;";
    sqlite3_stmt* stmt = nullptr;
    std::vector<std::string> members;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, group_name.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            members.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return members;
}
