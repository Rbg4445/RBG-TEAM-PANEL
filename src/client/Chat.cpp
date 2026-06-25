#include "Chat.h"
#include "Auth.h"
#include "Utils.h"
#include <iostream>

ClientChat& ClientChat::GetInstance() {
    static ClientChat instance;
    return instance;
}

bool ClientChat::Init() {
    if (enet_initialize() != 0) {
        std::cerr << "[ENet] An error occurred while initializing ENet." << std::endl;
        return false;
    }
    m_client = enet_host_create(nullptr, 10, 2, 0, 0);
    if (m_client == nullptr) {
        std::cerr << "[ENet] An error occurred while trying to create an ENet client host." << std::endl;
        enet_deinitialize();
        return false;
    }
    return true;
}

bool ClientChat::Connect(const std::string& hostIp, int port) {
    if (!m_client) return false;

    // Reset previous connection state
    ResetConnectionState();

    ENetAddress address;
    if (enet_address_set_host(&address, hostIp.c_str()) < 0) {
        std::cerr << "[ENet] Failed to resolve address: " << hostIp << std::endl;
        m_connectionFailed = true;
        return false;
    }
    address.port = port;

    std::cout << "[ENet] Connecting to server at " << hostIp << ":" << port << std::endl;
    m_peer = enet_host_connect(m_client, &address, 2, 0);
    if (m_peer == nullptr) {
        std::cerr << "[ENet] No available peers for initiating connection." << std::endl;
        m_connectionFailed = true;
        return false;
    }

    return true;
}

void ClientChat::Poll(int timeoutMs) {
    if (!m_client) return;

    ENetEvent event;
    while (enet_host_service(m_client, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                std::cout << "[ENet] Connection established." << std::endl;
                m_isConnected = true;
                m_connectionFailed = false;
                break;
            case ENET_EVENT_TYPE_RECEIVE: {
                std::string data((char*)event.packet->data);
                HandlePacket(data);
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "[ENet] Disconnected from server." << std::endl;
                m_isConnected = false;
                m_peer = nullptr;
                ClientAuth::GetInstance().ClearSession();
                m_messages.clear();
                m_onlineUsers.clear();
                break;
            default:
                break;
        }
    }
}

void ClientChat::Disconnect() {
    if (m_peer) {
        enet_peer_disconnect(m_peer, 0);
        enet_host_flush(m_client);
        m_peer = nullptr;
        m_isConnected = false;
        ClientAuth::GetInstance().ClearSession();
        m_messages.clear();
        m_onlineUsers.clear();
    }
}

void ClientChat::Close() {
    Disconnect();
    if (m_client) {
        enet_host_destroy(m_client);
        m_client = nullptr;
        enet_deinitialize();
    }
}

void ClientChat::ResetConnectionState() {
    m_isConnected = false;
    m_connectionFailed = false;
    m_loginError.clear();
    m_messages.clear();
    m_onlineUsers.clear();
    m_pendingUsers.clear();
    m_hasRegisterResponse = false;
    m_registerSuccess = false;
    m_registerStatus.clear();
    m_privateMessages.clear();
    m_typingUsers.clear();
    m_myAvatarId = 0;
}

void ClientChat::SendPacket(const std::string& type, const nlohmann::json& payload) {
    if (!m_peer) return;

    nlohmann::json envelope;
    envelope["type"] = type;
    envelope["payload"] = payload;

    std::string data = envelope.dump();
    ENetPacket* packet = enet_packet_create(data.c_str(), data.length() + 1, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, 0, packet);
    enet_host_flush(m_client);
}

void ClientChat::SendLogin(const std::string& username, const std::string& password) {
    std::string hash = Utils::Sha256(password);
    nlohmann::json payload = {
        {"username", username},
        {"password_hash", hash}
    };
    SendPacket("login", payload);
}

void ClientChat::SendChatMessage(const std::string& content, int64_t replyToId) {
    nlohmann::json payload = {
        {"content", content},
        {"reply_to_id", replyToId}
    };
    SendPacket("chat_msg", payload);
}

void ClientChat::SendKick(const std::string& username) {
    nlohmann::json payload = {
        {"username", username}
    };
    SendPacket("kick", payload);
}

void ClientChat::SendVoiceSignal(const nlohmann::json& data) {
    SendPacket("voice_signal", data);
}

void ClientChat::SendRegister(const std::string& username, const std::string& password) {
    std::string hash = Utils::Sha256(password);
    nlohmann::json payload = {
        {"username", username},
        {"password_hash", hash}
    };
    SendPacket("register", payload);
}

void ClientChat::SendApproveUser(const std::string& username) {
    nlohmann::json payload = {
        {"username", username}
    };
    SendPacket("approve_user", payload);
}

void ClientChat::SendRejectUser(const std::string& username) {
    nlohmann::json payload = {
        {"username", username}
    };
    SendPacket("reject_user", payload);
}

void ClientChat::HandlePacket(const std::string& data) {
    try {
        nlohmann::json envelope = nlohmann::json::parse(data);
        std::string type = envelope["type"];
        nlohmann::json payload = envelope["payload"];

        if (type == "login_response") {
            bool success = payload["success"];
            if (success) {
                std::string username = payload["username"];
                std::string role = payload["role"];
                m_myAvatarId = payload.value("avatar_id", 0);
                ClientAuth::GetInstance().SetSession(username, role);
                m_loginError.clear();
                std::cout << "[Auth] Successfully logged in as " << username << " (" << role << ") avatar: " << m_myAvatarId << std::endl;
            } else {
                m_loginError = payload["message"];
                std::cerr << "[Auth] Login failed: " << m_loginError << std::endl;
            }
        } else if (type == "chat_history") {
            m_messages.clear();
            for (const auto& item : payload) {
                ClientChatMessage msg;
                msg.id = item.value("id", (int64_t)0);
                msg.sender = item["sender"];
                msg.content = item["content"];
                msg.timestamp = item["timestamp"];
                msg.reply_to_id = item.value("reply_to_id", (int64_t)0);
                msg.reply_sender = item.value("reply_sender", "");
                msg.reply_content = item.value("reply_content", "");
                m_messages.push_back(msg);
            }
        } else if (type == "chat_msg") {
            ClientChatMessage msg;
            msg.id = payload.value("id", (int64_t)0);
            msg.sender = payload["sender"];
            msg.content = payload["content"];
            msg.timestamp = payload["timestamp"];
            msg.reply_to_id = payload.value("reply_to_id", (int64_t)0);
            msg.reply_sender = payload.value("reply_sender", "");
            msg.reply_content = payload.value("reply_content", "");
            m_messages.push_back(msg);
        } else if (type == "user_list") {
            m_onlineUsers.clear();
            for (const auto& item : payload) {
                OnlineUser user;
                user.username = item["username"];
                user.role = item["role"];
                user.avatar_id = item.value("avatar_id", 0);
                m_onlineUsers.push_back(user);
            }
        } else if (type == "private_msg") {
            PrivateMessage msg;
            msg.sender = payload["sender"];
            msg.to = payload["to"];
            msg.content = payload["content"];
            msg.timestamp = payload["timestamp"];
            m_privateMessages.push_back(msg);
        } else if (type == "dm_history") {
            std::string withUser = payload["with_user"];
            m_privateMessages.erase(std::remove_if(m_privateMessages.begin(), m_privateMessages.end(),
                [&](const PrivateMessage& m) { return m.sender == withUser || m.to == withUser; }),
                m_privateMessages.end());

            for (const auto& item : payload["messages"]) {
                PrivateMessage msg;
                msg.sender = item["sender"];
                msg.to = (msg.sender == withUser) ? ClientAuth::GetInstance().GetUsername() : withUser;
                msg.content = item["content"];
                msg.timestamp = item["timestamp"];
                m_privateMessages.push_back(msg);
            }
        } else if (type == "typing_status") {
            std::string user = payload["username"];
            bool isTyping = payload["is_typing"];
            auto it = std::find(m_typingUsers.begin(), m_typingUsers.end(), user);
            if (isTyping) {
                if (it == m_typingUsers.end()) {
                    m_typingUsers.push_back(user);
                }
            } else {
                if (it != m_typingUsers.end()) {
                    m_typingUsers.erase(it);
                }
            }
        } else if (type == "voice_signal") {
            if (m_voiceSignalCallback) {
                m_voiceSignalCallback(payload);
            }
        } else if (type == "register_response") {
            bool success = payload["success"];
            m_registerSuccess = success;
            m_registerStatus = payload["message"];
            m_hasRegisterResponse = true;
        } else if (type == "pending_users_list") {
            m_pendingUsers.clear();
            for (const auto& item : payload) {
                m_pendingUsers.push_back(item.get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ENet] Error parsing client packet: " << e.what() << std::endl;
    }
}

void ClientChat::SendPrivateMessage(const std::string& toUser, const std::string& content) {
    nlohmann::json payload = {
        {"to", toUser},
        {"content", content}
    };
    SendPacket("private_msg", payload);
}

void ClientChat::SendGetDMHistory(const std::string& withUser) {
    nlohmann::json payload = {
        {"with_user", withUser}
    };
    SendPacket("get_dm_history", payload);
}

void ClientChat::SendChangeAvatar(int avatarId) {
    nlohmann::json payload = {
        {"avatar_id", avatarId}
    };
    SendPacket("change_avatar", payload);
}

void ClientChat::SendTypingStatus(bool isTyping) {
    nlohmann::json payload = {
        {"is_typing", isTyping}
    };
    SendPacket("typing_status", payload);
}
