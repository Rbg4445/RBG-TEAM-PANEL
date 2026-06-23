#ifndef CLIENT_CHAT_H
#define CLIENT_CHAT_H

#include <string>
#include <vector>
#include <functional>
#include <enet/enet.h>
#include <nlohmann/json.hpp>

struct ClientChatMessage {
    int64_t id = 0;
    std::string sender;
    std::string content;
    int64_t timestamp = 0;
    int64_t reply_to_id = 0;
    std::string reply_sender;
    std::string reply_content;
};

struct OnlineUser {
    std::string username;
    std::string role;
};

class ClientChat {
public:
    static ClientChat& GetInstance();

    bool Init();
    bool Connect(const std::string& hostIp, int port = 7777);
    void Poll(int timeoutMs = 10);
    void Disconnect();
    void Close();

    // Actions
    void SendLogin(const std::string& username, const std::string& password);
    void SendRegister(const std::string& username, const std::string& password);
    void SendApproveUser(const std::string& username);
    void SendRejectUser(const std::string& username);
    void SendChatMessage(const std::string& content, int64_t replyToId = 0);
    void SendKick(const std::string& username);
    void SendVoiceSignal(const nlohmann::json& data);

    // Voice signal callback
    using VoiceSignalCallback = std::function<void(const nlohmann::json&)>;
    void SetVoiceSignalCallback(VoiceSignalCallback cb) { m_voiceSignalCallback = cb; }

    // Getters for UI
    bool IsConnected() const { return m_peer != nullptr && m_isConnected; }
    uint32_t GetPing() const { return m_peer ? m_peer->roundTripTime : 0; }
    bool HasConnectionFailed() const { return m_connectionFailed; }
    const std::string& GetLoginError() const { return m_loginError; }
    const std::vector<ClientChatMessage>& GetMessages() const { return m_messages; }
    const std::vector<OnlineUser>& GetOnlineUsers() const { return m_onlineUsers; }
    const std::vector<std::string>& GetPendingUsers() const { return m_pendingUsers; }
    bool HasRegisterResponse() const { return m_hasRegisterResponse; }
    bool IsRegisterSuccess() const { return m_registerSuccess; }
    const std::string& GetRegisterMessage() const { return m_registerStatus; }
    void ClearRegisterResponse() { m_hasRegisterResponse = false; m_registerStatus.clear(); m_registerSuccess = false; }

    // Reset helper
    void ResetConnectionState();

private:
    ClientChat() = default;
    ~ClientChat() = default;
    ClientChat(const ClientChat&) = delete;
    ClientChat& operator=(const ClientChat&) = delete;

    void SendPacket(const std::string& type, const nlohmann::json& payload);
    void HandlePacket(const std::string& data);

    ENetHost* m_client = nullptr;
    ENetPeer* m_peer = nullptr;
    bool m_isConnected = false;
    bool m_connectionFailed = false;

    // Session State
    std::string m_loginError;
    std::vector<ClientChatMessage> m_messages;
    std::vector<OnlineUser> m_onlineUsers;
    VoiceSignalCallback m_voiceSignalCallback;
    std::vector<std::string> m_pendingUsers;
    bool m_hasRegisterResponse = false;
    bool m_registerSuccess = false;
    std::string m_registerStatus;
};

#endif // CLIENT_CHAT_H
