#include "Auth.h"
#include "DB.h"

namespace ServerAuth {
    bool Login(const std::string& username, const std::string& passwordHash, std::string& outRole, int& outApproved, int& outAvatarId) {
        return DB::GetInstance().VerifyUser(username, passwordHash, outRole, outApproved, outAvatarId);
    }
}
