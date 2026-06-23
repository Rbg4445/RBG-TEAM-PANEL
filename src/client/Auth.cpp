#include "Auth.h"

ClientAuth& ClientAuth::GetInstance() {
    static ClientAuth instance;
    return instance;
}

void ClientAuth::SetSession(const std::string& username, const std::string& role) {
    m_username = username;
    m_role = role;
    m_loggedIn = true;
}

void ClientAuth::ClearSession() {
    m_username.clear();
    m_role.clear();
    m_loggedIn = false;
}
