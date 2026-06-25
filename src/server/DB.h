#ifndef DB_H
#define DB_H

#include <string>
#include <vector>
#include <cstdint>

struct ChatMessage {
    int64_t id = 0;
    std::string sender;
    std::string content;
    int64_t timestamp = 0;
    int64_t reply_to_id = 0;
};

class DB {
public:
    static DB& GetInstance();

    bool Init(const std::string& dbPath);
    void Close();

    // User operations
    bool CreateUser(const std::string& username, const std::string& passwordHash, const std::string& role, int approved = 0);
    bool VerifyUser(const std::string& username, const std::string& passwordHash, std::string& outRole, int& outApproved, int& outAvatarId);
    bool UpdateUserAvatar(const std::string& username, int avatarId);
    bool UserExists(const std::string& username);
    bool GetMessageById(int64_t id, ChatMessage& outMsg);
    std::vector<std::string> GetPendingUsers();
    bool ApproveUser(const std::string& username);
    bool DeleteUser(const std::string& username);

    // Message operations
    bool SaveMessage(const std::string& room, const std::string& sender, const std::string& content, int64_t timestamp, int64_t reply_to_id = 0, int64_t* out_id = nullptr);
    std::vector<ChatMessage> GetLastMessages(const std::string& room, int limit = 50);

private:
    DB() = default;
    ~DB() = default;
    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    void* m_db = nullptr; // SQLite3 connection pointer (void* to avoid exposing sqlite3 headers in DB.h)
};

#endif // DB_H
