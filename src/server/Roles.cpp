#include "Roles.h"
#include <map>

namespace ServerRoles {
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

    // Helper to map role to a hierarchy level (larger means higher authority)
    int GetRoleLevel(const std::string& role) {
        if (role == ROLE_RBG) return 5;
        if (role == ROLE_OWNER) return 4;
        if (role == ROLE_ADMIN) return 3;
        if (role == ROLE_MOD) return 2;
        if (role == ROLE_USER) return 1;
        return 0; // Unknown role
    }

    bool HasPermission(const std::string& role, const std::string& action) {
        int level = GetRoleLevel(role);

        if (action == ACTION_SEND_MESSAGE) {
            return level >= 1; // All roles can send messages
        }
        if (action == ACTION_DELETE_MESSAGE) {
            return level >= 2; // Mods and above can delete messages
        }
        if (action == ACTION_KICK_USER || action == ACTION_MUTE_USER) {
            return level >= 2; // Mods and above can kick/mute
        }
        if (action == ACTION_MANAGE_ROLES) {
            return level >= 5; // Only RBG can manage roles
        }

        return false;
    }

    bool IsSuperior(const std::string& roleA, const std::string& roleB) {
        return GetRoleLevel(roleA) > GetRoleLevel(roleB);
    }
}
