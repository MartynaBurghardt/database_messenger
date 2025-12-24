#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>

struct UserRecord {
    std::vector<unsigned char> salt;
    std::vector<unsigned char> hash;
};

struct MessageRecord {
    std::string from;
    std::string to;
    std::string content;
    std::string ts;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
     bool create_user(const std::string& username,
                      const std::vector<unsigned char>& salt,
                      const std::vector<unsigned char>& hash);
     std::optional<UserRecord> get_user(const std::string& username);
     void save_message(const std::string& from,
                       const std::string& to,
                       const std::string& content);
     std::vector<MessageRecord> get_history(const std::string& user,int limit = 20);
     std::vector<MessageRecord> get_undelivered(const std::string& user);
     void mark_delivered(const std::string& user);

private:
    sqlite3* db_;
};

