#ifndef CLIENT_AUTH_H
#define CLIENT_AUTH_H

#include <string>

class ClientAuth {
public:
    static ClientAuth& GetInstance();

    bool IsLoggedIn() const { return m_loggedIn; }
    const std::string& GetUsername() const { return m_username; }
    const std::string& GetRole() const { return m_role; }

    void SetSession(const std::string& username, const std::string& role);
    void ClearSession();

private:
    ClientAuth() = default;
    ~ClientAuth() = default;
    ClientAuth(const ClientAuth&) = delete;
    ClientAuth& operator=(const ClientAuth&) = delete;

    bool m_loggedIn = false;
    std::string m_username;
    std::string m_role;
};

#endif // CLIENT_AUTH_H
