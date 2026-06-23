#include "Roles.h"

namespace ClientRoles {
    const std::string ACTION_SEND_MESSAGE = "send_message";
    const std::string ACTION_DELETE_MESSAGE = "delete_message";
    const std::string ACTION_KICK_USER = "kick_user";
    const std::string ACTION_MUTE_USER = "mute_user";
    const std::string ACTION_MANAGE_ROLES = "manage_roles";

    const std::string ROLE_RBG = "RBG";
    const std::string ROLE_OWNER = "Owner";
    const std::string ROLE_ADMIN = "Admin";
    const std::string ROLE_MOD = "Mod";
    const std::string ROLE_USER = "User";

    int GetRoleLevel(const std::string& role) {
        if (role == ROLE_RBG) return 5;
        if (role == ROLE_OWNER) return 4;
        if (role == ROLE_ADMIN) return 3;
        if (role == ROLE_MOD) return 2;
        if (role == ROLE_USER) return 1;
        return 0;
    }

    bool HasPermission(const std::string& role, const std::string& action) {
        int level = GetRoleLevel(role);

        if (action == ACTION_SEND_MESSAGE) {
            return level >= 1;
        }
        if (action == ACTION_DELETE_MESSAGE) {
            return level >= 2;
        }
        if (action == ACTION_KICK_USER || action == ACTION_MUTE_USER) {
            return level >= 2;
        }
        if (action == ACTION_MANAGE_ROLES) {
            return level >= 5;
        }

        return false;
    }
}
