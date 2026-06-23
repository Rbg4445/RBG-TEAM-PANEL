#include "Auth.h"
#include "DB.h"

namespace ServerAuth {
    bool Login(const std::string& username, const std::string& passwordHash, std::string& outRole) {
        return DB::GetInstance().VerifyUser(username, passwordHash, outRole);
    }
}
