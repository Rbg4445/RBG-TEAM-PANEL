#ifndef CLIENT_ROLES_H
#define CLIENT_ROLES_H

#include <string>

namespace ClientRoles {
    // Actions
    extern const std::string ACTION_SEND_MESSAGE;
    extern const std::string ACTION_DELETE_MESSAGE;
    extern const std::string ACTION_KICK_USER;
    extern const std::string ACTION_MUTE_USER;
    extern const std::string ACTION_MANAGE_ROLES;

    // Roles
    extern const std::string ROLE_RBG;
    extern const std::string ROLE_OWNER;
    extern const std::string ROLE_ADMIN;
    extern const std::string ROLE_MOD;
    extern const std::string ROLE_USER;

    // Checks if a given role is allowed to perform an action
    bool HasPermission(const std::string& role, const std::string& action);
}

#endif // CLIENT_ROLES_H
