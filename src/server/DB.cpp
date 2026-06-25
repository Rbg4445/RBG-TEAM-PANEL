#include "DB.h"
#include "Utils.h"
#include <sqlite3.h>
#include <iostream>
#include <algorithm>

// Helper macro to cast m_db to sqlite3*
#define SQLITE_DB ((sqlite3*)m_db)

DB& DB::GetInstance() {
    static DB instance;
    return instance;
}

bool DB::Init(const std::string& dbPath) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }
    m_db = db;

    // Create tables
    const char* createUsersTable = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL"
        ");";

    const char* createMessagesTable =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "room TEXT NOT NULL,"
        "sender TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "timestamp INTEGER NOT NULL"
        ");";

    char* errMsg = nullptr;
    rc = sqlite3_exec(SQLITE_DB, createUsersTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error (users table): " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    rc = sqlite3_exec(SQLITE_DB, createMessagesTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error (messages table): " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    // Alter table to add reply_to_id if it doesn't exist
    sqlite3_exec(SQLITE_DB, "ALTER TABLE messages ADD COLUMN reply_to_id INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);

    // Alter table to add approved if it doesn't exist
    sqlite3_exec(SQLITE_DB, "ALTER TABLE users ADD COLUMN approved INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);

    // Alter table to add avatar_id if it doesn't exist
    sqlite3_exec(SQLITE_DB, "ALTER TABLE users ADD COLUMN avatar_id INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);

    // Update existing default accounts to be approved
    sqlite3_exec(SQLITE_DB, "UPDATE users SET approved = 1 WHERE username IN ('admin', 'owner', 'admin_user', 'mod', 'user');", nullptr, nullptr, nullptr);

    // Seed default accounts if they don't exist
    if (!UserExists("admin")) {
        std::string defaultHash = Utils::Sha256("admin123");
        std::cout << "[DB] Seeding default admin user (username: 'admin', role: 'RBG')" << std::endl;
        CreateUser("admin", defaultHash, "RBG", 1);
    }
    if (!UserExists("owner")) {
        std::string defaultHash = Utils::Sha256("owner123");
        std::cout << "[DB] Seeding default owner user (username: 'owner', role: 'Owner')" << std::endl;
        CreateUser("owner", defaultHash, "Owner", 1);
    }
    if (!UserExists("admin_user")) {
        std::string defaultHash = Utils::Sha256("admin123");
        std::cout << "[DB] Seeding default admin_user user (username: 'admin_user', role: 'Admin')" << std::endl;
        CreateUser("admin_user", defaultHash, "Admin", 1);
    }
    if (!UserExists("mod")) {
        std::string defaultHash = Utils::Sha256("mod123");
        std::cout << "[DB] Seeding default mod user (username: 'mod', role: 'Mod')" << std::endl;
        CreateUser("mod", defaultHash, "Mod", 1);
    }
    if (!UserExists("user")) {
        std::string defaultHash = Utils::Sha256("user123");
        std::cout << "[DB] Seeding default user (username: 'user', role: 'User')" << std::endl;
        CreateUser("user", defaultHash, "User", 1);
    }

    return true;
}

void DB::Close() {
    if (m_db) {
        sqlite3_close(SQLITE_DB);
        m_db = nullptr;
    }
}

bool DB::CreateUser(const std::string& username, const std::string& passwordHash, const std::string& role, int approved) {
    const char* sql = "INSERT INTO users (username, password_hash, role, approved) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare insert user error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, approved);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Execute insert user error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return false;
    }

    return true;
}

bool DB::VerifyUser(const std::string& username, const std::string& passwordHash, std::string& outRole, int& outApproved, int& outAvatarId) {
    const char* sql = "SELECT role, approved, avatar_id FROM users WHERE username = ? AND password_hash = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare verify user error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    bool success = false;
    if (rc == SQLITE_ROW) {
        const unsigned char* roleText = sqlite3_column_text(stmt, 0);
        if (roleText) {
            outRole = (const char*)roleText;
            outApproved = sqlite3_column_int(stmt, 1);
            outAvatarId = sqlite3_column_int(stmt, 2);
            success = true;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool DB::UpdateUserAvatar(const std::string& username, int avatarId) {
    const char* sql = "UPDATE users SET avatar_id = ? WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare update avatar error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return false;
    }

    sqlite3_bind_int(stmt, 1, avatarId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE);
}

bool DB::UserExists(const std::string& username) {
    const char* sql = "SELECT 1 FROM users WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    bool exists = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    return exists;
}

bool DB::GetMessageById(int64_t id, ChatMessage& outMsg) {
    const char* sql = "SELECT sender, content, timestamp, reply_to_id FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    bool success = false;
    if (rc == SQLITE_ROW) {
        outMsg.id = id;
        const unsigned char* senderText = sqlite3_column_text(stmt, 0);
        const unsigned char* contentText = sqlite3_column_text(stmt, 1);
        outMsg.timestamp = sqlite3_column_int64(stmt, 2);
        outMsg.reply_to_id = sqlite3_column_int64(stmt, 3);

        if (senderText) outMsg.sender = (const char*)senderText;
        if (contentText) outMsg.content = (const char*)contentText;
        success = true;
    }

    sqlite3_finalize(stmt);
    return success;
}

bool DB::SaveMessage(const std::string& room, const std::string& sender, const std::string& content, int64_t timestamp, int64_t reply_to_id, int64_t* out_id) {
    const char* sql = "INSERT INTO messages (room, sender, content, timestamp, reply_to_id) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare save message error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, timestamp);
    sqlite3_bind_int64(stmt, 5, reply_to_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Execute save message error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return false;
    }

    if (out_id) {
        *out_id = (int64_t)sqlite3_last_insert_rowid(SQLITE_DB);
    }
    return true;
}

std::vector<ChatMessage> DB::GetLastMessages(const std::string& room, int limit) {
    std::vector<ChatMessage> messages;
    const char* sql = "SELECT id, sender, content, timestamp, reply_to_id FROM messages WHERE room = ? ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare query messages error: " << sqlite3_errmsg(SQLITE_DB) << std::endl;
        return messages;
    }

    sqlite3_bind_text(stmt, 1, room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChatMessage msg;
        int64_t id = sqlite3_column_int64(stmt, 0);
        const unsigned char* senderText = sqlite3_column_text(stmt, 1);
        const unsigned char* contentText = sqlite3_column_text(stmt, 2);
        int64_t timestamp = sqlite3_column_int64(stmt, 3);
        int64_t replyToId = sqlite3_column_int64(stmt, 4);

        msg.id = id;
        if (senderText) msg.sender = (const char*)senderText;
        if (contentText) msg.content = (const char*)contentText;
        msg.timestamp = timestamp;
        msg.reply_to_id = replyToId;

        messages.push_back(msg);
    }

    sqlite3_finalize(stmt);

    // Reverse messages to display in chronological order
    std::reverse(messages.begin(), messages.end());
    return messages;
}

std::vector<std::string> DB::GetPendingUsers() {
    std::vector<std::string> users;
    const char* sql = "SELECT username FROM users WHERE approved = 0 ORDER BY username ASC;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* usernameText = sqlite3_column_text(stmt, 0);
            if (usernameText) {
                users.push_back((const char*)usernameText);
            }
        }
    }
    sqlite3_finalize(stmt);
    return users;
}

bool DB::ApproveUser(const std::string& username) {
    const char* sql = "UPDATE users SET approved = 1 WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool DB::DeleteUser(const std::string& username) {
    const char* sql = "DELETE FROM users WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(SQLITE_DB, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}
