#include "Chat.h"
#include "Auth.h"
#include "DB.h"
#include "Utils.h"
#include "Roles.h"
#include <iostream>

ServerChat& ServerChat::GetInstance() {
    static ServerChat instance;
    return instance;
}

bool ServerChat::Init(int port) {
    if (enet_initialize() != 0) {
        std::cerr << "[ENet] An error occurred while initializing ENet." << std::endl;
        return false;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    m_server = enet_host_create(&address, 32, 2, 0, 0);
    if (m_server == nullptr) {
        std::cerr << "[ENet] An error occurred while trying to create an ENet server host." << std::endl;
        enet_deinitialize();
        return false;
    }

    std::cout << "[ENet] Server listening on port " << port << "/UDP" << std::endl;
    return true;
}

void ServerChat::Poll(int timeoutMs) {
    if (!m_server) return;

    ENetEvent event;
    while (enet_host_service(m_server, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                char ip[64];
                enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));
                std::cout << "[ENet] A new client connected from " << ip << ":" << event.peer->address.port << std::endl;
                
                // Initialize session
                ClientSession session;
                session.authenticated = false;
                m_sessions[event.peer] = session;
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                std::string data((char*)event.packet->data);
                HandlePacket(event.peer, data);
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                std::cout << "[ENet] Client disconnected." << std::endl;
                auto it = m_sessions.find(event.peer);
                if (it != m_sessions.end()) {
                    bool wasAuth = it->second.authenticated;
                    std::string username = it->second.username;
                    m_sessions.erase(it);

                    if (wasAuth) {
                        // Broadcast updated user list
                        nlohmann::json userList = nlohmann::json::array();
                        for (const auto& [peer, session] : m_sessions) {
                            if (session.authenticated) {
                                userList.push_back({{"username", session.username}, {"role", session.role}});
                            }
                        }
                        BroadcastMessage("user_list", userList);

                        // Notify chat of user departure
                        nlohmann::json systemMsg = {
                            {"sender", "[Sistem]"},
                            {"content", username + " ayrıldı."},
                            {"timestamp", Utils::GetCurrentTimestamp()}
                        };
                        BroadcastMessage("chat_msg", systemMsg);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void ServerChat::Close() {
    if (m_server) {
        enet_host_destroy(m_server);
        m_server = nullptr;
        enet_deinitialize();
    }
}

void ServerChat::SendPacket(ENetPeer* peer, const std::string& type, const nlohmann::json& payload) {
    nlohmann::json envelope;
    envelope["type"] = type;
    envelope["payload"] = payload;

    std::string data = envelope.dump();
    ENetPacket* packet = enet_packet_create(data.c_str(), data.length() + 1, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, packet);
    enet_host_flush(m_server);
}

void ServerChat::BroadcastMessage(const std::string& type, const nlohmann::json& payload, ENetPeer* excludePeer) {
    for (auto const& [peer, session] : m_sessions) {
        if (session.authenticated && peer != excludePeer) {
            SendPacket(peer, type, payload);
        }
    }
}

void ServerChat::HandlePacket(ENetPeer* peer, const std::string& data) {
    try {
        nlohmann::json envelope = nlohmann::json::parse(data);
        std::string type = envelope["type"];
        nlohmann::json payload = envelope["payload"];

        if (type == "login") {
            HandleLogin(peer, payload);
        } else if (type == "register") {
            HandleRegister(peer, payload);
        } else if (type == "approve_user") {
            HandleApproveUser(peer, payload);
        } else if (type == "reject_user") {
            HandleRejectUser(peer, payload);
        } else if (type == "chat_msg") {
            HandleChatMessage(peer, payload);
        } else if (type == "kick") {
            HandleKick(peer, payload);
        } else if (type == "voice_signal") {
            HandleVoiceSignal(peer, payload);
        }
    } catch (const std::exception& e) {
        std::cerr << "[ENet] Error parsing packet: " << e.what() << std::endl;
    }
}

void ServerChat::HandleLogin(ENetPeer* peer, const nlohmann::json& data) {
    std::string username = data["username"];
    std::string passwordHash = data["password_hash"];

    std::string role;
    int approved = 0;
    if (ServerAuth::Login(username, passwordHash, role, approved)) {
        if (approved == 0) {
            std::cout << "[ENet] Login denied (unapproved) for user: " << username << std::endl;
            nlohmann::json response = {
                {"success", false},
                {"message", "Hesabınız henüz onaylanmadı. Lütfen admin onayını bekleyin."}
            };
            SendPacket(peer, "login_response", response);
            return;
        }

        std::cout << "[ENet] Login success for user: " << username << " (" << role << ")" << std::endl;
        
        m_sessions[peer].authenticated = true;
        m_sessions[peer].username = username;
        m_sessions[peer].role = role;

        // Respond success
        nlohmann::json response = {
            {"success", true},
            {"username", username},
            {"role", role}
        };
        SendPacket(peer, "login_response", response);

        // If the user is admin/mod, send pending users list
        if (role == "RBG" || role == "Owner" || role == "Admin" || role == "Mod") {
            SendPendingUsersListToAdmins();
        }

        // Send history of last 50 messages
        std::vector<ChatMessage> history = DB::GetInstance().GetLastMessages("global", 50);
        nlohmann::json historyArray = nlohmann::json::array();
        for (const auto& msg : history) {
            nlohmann::json item = {
                {"id", msg.id},
                {"sender", msg.sender},
                {"content", msg.content},
                {"timestamp", msg.timestamp},
                {"reply_to_id", msg.reply_to_id}
            };
            if (msg.reply_to_id > 0) {
                ChatMessage replyMsg;
                if (DB::GetInstance().GetMessageById(msg.reply_to_id, replyMsg)) {
                    item["reply_sender"] = replyMsg.sender;
                    item["reply_content"] = replyMsg.content;
                }
            }
            historyArray.push_back(item);
        }
        SendPacket(peer, "chat_history", historyArray);

        // Broadcast updated user list
        nlohmann::json userList = nlohmann::json::array();
        for (const auto& [p, session] : m_sessions) {
            if (session.authenticated) {
                userList.push_back({{"username", session.username}, {"role", session.role}});
            }
        }
        BroadcastMessage("user_list", userList);

        // Notify chat of user joining
        nlohmann::json systemMsg = {
            {"sender", "[Sistem]"},
            {"content", username + " katıldı."},
            {"timestamp", Utils::GetCurrentTimestamp()}
        };
        BroadcastMessage("chat_msg", systemMsg);

    } else {
        std::cout << "[ENet] Login failed for user: " << username << std::endl;
        nlohmann::json response = {
            {"success", false},
            {"message", "Geçersiz kullanıcı adı veya şifre."}
        };
        SendPacket(peer, "login_response", response);
    }
}

void ServerChat::HandleChatMessage(ENetPeer* peer, const nlohmann::json& data) {
    auto it = m_sessions.find(peer);
    if (it == m_sessions.end() || !it->second.authenticated) {
        std::cerr << "[ENet] Unauthenticated client tried to send message." << std::endl;
        return;
    }

    std::string sender = it->second.username;
    std::string content = data["content"];
    int64_t reply_to_id = data.value("reply_to_id", 0);
    int64_t timestamp = Utils::GetCurrentTimestamp();

    // Persist to database
    int64_t inserted_id = 0;
    DB::GetInstance().SaveMessage("global", sender, content, timestamp, reply_to_id, &inserted_id);

    // Broadcast to everyone
    nlohmann::json msg = {
        {"id", inserted_id},
        {"sender", sender},
        {"content", content},
        {"timestamp", timestamp},
        {"reply_to_id", reply_to_id}
    };
    if (reply_to_id > 0) {
        ChatMessage replyMsg;
        if (DB::GetInstance().GetMessageById(reply_to_id, replyMsg)) {
            msg["reply_sender"] = replyMsg.sender;
            msg["reply_content"] = replyMsg.content;
        }
    }
    BroadcastMessage("chat_msg", msg);
}

void ServerChat::HandleKick(ENetPeer* peer, const nlohmann::json& data) {
    auto it = m_sessions.find(peer);
    if (it == m_sessions.end() || !it->second.authenticated) {
        std::cerr << "[ENet] Unauthenticated client tried to kick." << std::endl;
        return;
    }

    std::string senderUsername = it->second.username;
    std::string senderRole = it->second.role;

    if (!ServerRoles::HasPermission(senderRole, ServerRoles::ACTION_KICK_USER)) {
        std::cerr << "[ENet] User " << senderUsername << " doesn't have permission to kick." << std::endl;
        return;
    }

    std::string targetUsername = data["username"];

    // Find target peer
    ENetPeer* targetPeer = nullptr;
    std::string targetRole;
    for (const auto& [p, session] : m_sessions) {
        if (session.authenticated && session.username == targetUsername) {
            targetPeer = p;
            targetRole = session.role;
            break;
        }
    }

    if (!targetPeer) {
        std::cerr << "[ENet] Target user " << targetUsername << " not found or not online." << std::endl;
        return;
    }

    // Check hierarchy
    if (!ServerRoles::IsSuperior(senderRole, targetRole)) {
        std::cerr << "[ENet] User " << senderUsername << " (" << senderRole 
                  << ") cannot kick " << targetUsername << " (" << targetRole << ") due to hierarchy." << std::endl;
        return;
    }

    std::cout << "[ENet] User " << targetUsername << " is being kicked by " << senderUsername << std::endl;

    // Notify chat of user being kicked
    nlohmann::json systemMsg = {
        {"sender", "[Sistem]"},
        {"content", targetUsername + " sunucudan atildi (" + senderUsername + " tarafindan)."},
        {"timestamp", Utils::GetCurrentTimestamp()}
    };
    BroadcastMessage("chat_msg", systemMsg);

    // Disconnect target
    enet_peer_disconnect(targetPeer, 0);
    enet_host_flush(m_server);
}

void ServerChat::HandleVoiceSignal(ENetPeer* peer, const nlohmann::json& data) {
    auto it = m_sessions.find(peer);
    if (it == m_sessions.end() || !it->second.authenticated) return;

    std::string senderUsername = it->second.username;
    std::string targetUsername = data.value("target", "");
    if (targetUsername.empty()) return;

    // Find target peer
    ENetPeer* targetPeer = nullptr;
    for (const auto& [p, session] : m_sessions) {
        if (session.authenticated && session.username == targetUsername) {
            targetPeer = p;
            break;
        }
    }

    if (!targetPeer) return;

    // Forward signal with sender identity
    nlohmann::json forward = data;
    forward["sender"] = senderUsername;
    SendPacket(targetPeer, "voice_signal", forward);
}

void ServerChat::HandleRegister(ENetPeer* peer, const nlohmann::json& payload) {
    std::string username = payload["username"];
    std::string passwordHash = payload["password_hash"];

    if (username.empty() || passwordHash.empty()) {
        nlohmann::json response = {
            {"success", false},
            {"message", "Kullanıcı adı veya şifre boş olamaz."}
        };
        SendPacket(peer, "register_response", response);
        return;
    }

    if (DB::GetInstance().UserExists(username)) {
        nlohmann::json response = {
            {"success", false},
            {"message", "Bu kullanıcı adı zaten alınmış."}
        };
        SendPacket(peer, "register_response", response);
        return;
    }

    // Create user with default role "User" and approved = 0
    if (DB::GetInstance().CreateUser(username, passwordHash, "User", 0)) {
        std::cout << "[ENet] New user registered: " << username << " (pending approval)" << std::endl;
        nlohmann::json response = {
            {"success", true},
            {"message", "Kayıt başarılı! Admin onayından sonra giriş yapabilirsiniz."}
        };
        SendPacket(peer, "register_response", response);

        // Broadcast updated pending users list to all online admins/mods
        SendPendingUsersListToAdmins();
    } else {
        nlohmann::json response = {
            {"success", false},
            {"message", "Kullanıcı oluşturulurken veritabanı hatası oluştu."}
        };
        SendPacket(peer, "register_response", response);
    }
}

void ServerChat::HandleApproveUser(ENetPeer* peer, const nlohmann::json& payload) {
    auto it = m_sessions.find(peer);
    if (it == m_sessions.end() || !it->second.authenticated) return;

    std::string senderRole = it->second.role;
    if (senderRole != "RBG" && senderRole != "Owner" && senderRole != "Admin" && senderRole != "Mod") return;

    std::string targetUser = payload["username"];
    if (DB::GetInstance().ApproveUser(targetUser)) {
        std::cout << "[ENet] User '" << targetUser << "' approved by admin '" << it->second.username << "'" << std::endl;
        SendPendingUsersListToAdmins();
    }
}

void ServerChat::HandleRejectUser(ENetPeer* peer, const nlohmann::json& payload) {
    auto it = m_sessions.find(peer);
    if (it == m_sessions.end() || !it->second.authenticated) return;

    std::string senderRole = it->second.role;
    if (senderRole != "RBG" && senderRole != "Owner" && senderRole != "Admin" && senderRole != "Mod") return;

    std::string targetUser = payload["username"];
    if (DB::GetInstance().DeleteUser(targetUser)) {
        std::cout << "[ENet] User '" << targetUser << "' rejected (deleted) by admin '" << it->second.username << "'" << std::endl;
        SendPendingUsersListToAdmins();
    }
}

void ServerChat::SendPendingUsersListToAdmins() {
    std::vector<std::string> pendingUsers = DB::GetInstance().GetPendingUsers();
    nlohmann::json payload = pendingUsers;

    for (auto const& [peer, session] : m_sessions) {
        if (session.authenticated && (session.role == "RBG" || session.role == "Owner" || session.role == "Admin" || session.role == "Mod")) {
            SendPacket(peer, "pending_users_list", payload);
        }
    }
}
