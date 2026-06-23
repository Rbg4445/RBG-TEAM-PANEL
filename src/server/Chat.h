#ifndef SERVER_CHAT_H
#define SERVER_CHAT_H

#include <string>
#include <map>
#include <enet/enet.h>
#include <nlohmann/json.hpp>

struct ClientSession {
    std::string username;
    std::string role;
    bool authenticated = false;
};

class ServerChat {
public:
    static ServerChat& GetInstance();

    bool Init(int port = 7777);
    void Poll(int timeoutMs = 10);
    void Close();

    void BroadcastMessage(const std::string& type, const nlohmann::json& payload, ENetPeer* excludePeer = nullptr);
    void SendPacket(ENetPeer* peer, const std::string& type, const nlohmann::json& payload);

private:
    ServerChat() = default;
    ~ServerChat() = default;
    ServerChat(const ServerChat&) = delete;
    ServerChat& operator=(const ServerChat&) = delete;

    void HandlePacket(ENetPeer* peer, const std::string& data);
    void HandleLogin(ENetPeer* peer, const nlohmann::json& data);
    void HandleRegister(ENetPeer* peer, const nlohmann::json& data);
    void HandleApproveUser(ENetPeer* peer, const nlohmann::json& data);
    void HandleRejectUser(ENetPeer* peer, const nlohmann::json& data);
    void HandleChatMessage(ENetPeer* peer, const nlohmann::json& data);
    void HandleKick(ENetPeer* peer, const nlohmann::json& data);
    void HandleVoiceSignal(ENetPeer* peer, const nlohmann::json& data);
    void SendPendingUsersListToAdmins();

    ENetHost* m_server = nullptr;
    std::map<ENetPeer*, ClientSession> m_sessions;
};

#endif // SERVER_CHAT_H
