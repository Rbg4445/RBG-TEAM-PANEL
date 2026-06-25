#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H

#include <string>

namespace ServerAuth {
    // Verifies credentials against the SQLite database and returns the user's role if successful.
    bool Login(const std::string& username, const std::string& passwordHash, std::string& outRole, int& outApproved, int& outAvatarId);
}

#endif // SERVER_AUTH_H
