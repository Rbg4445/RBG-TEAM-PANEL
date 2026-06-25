#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "Voice.h"
#include "Chat.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------
enum class ToastType { INFO, SUCCESS, WARNING, ERR };
extern void AddToast(const std::string& msg, ToastType type = ToastType::INFO, float dur = 3.0f);

// ---------------------------------------------------------------
// Audio callback — Fluxer: duplex device, mix all incoming peers
// ---------------------------------------------------------------
void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ClientVoice* pVoice = (ClientVoice*)pDevice->pUserData;
    if (!pVoice) return;

    // Send captured mic audio to all room peers (if not muted)
    if (pInput && !pVoice->IsMuted()) {
        pVoice->SendAudioFrameToAll(pInput, frameCount * sizeof(int16_t));
    }

    // Legacy 1:1 call audio send
    if (pInput && pVoice->IsInCall() && !pVoice->IsMuted()) {
        // handled via SendAudioFrameToAll which checks IsInCall too
    }

    // Output: mix all incoming peer buffers
    if (pOutput && !pVoice->IsDeafened()) {
        pVoice->ReadMixedAudioFrame(pOutput, frameCount * sizeof(int16_t));
    } else if (pOutput) {
        memset(pOutput, 0, frameCount * sizeof(int16_t));
    }
}

// ---------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------
ClientVoice& ClientVoice::GetInstance() {
    static ClientVoice instance;
    return instance;
}

// ---------------------------------------------------------------
// Init — register signaling callback with ClientChat
// ---------------------------------------------------------------
bool ClientVoice::Init() {
    ClientChat::GetInstance().SetVoiceSignalCallback([this](const nlohmann::json& payload) {
        HandleSignalingMessage(payload);
    });
    ClientChat::GetInstance().SetVoiceRoomCallback([this](const nlohmann::json& payload) {
        HandleVoiceRoomUpdate(payload);
    });
    m_mixBuffer.Init(24000 * 4);
    return true;
}

void ClientVoice::Register(const std::string& username) {
    m_username = username;
}

void ClientVoice::Close() {
    LeaveRoom();
    StopCall();
}

// ---------------------------------------------------------------
// Room join / leave  (Fluxer: connection.connect / disconnect)
// ---------------------------------------------------------------
void ClientVoice::JoinRoom() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_inRoom) return;
    m_inRoom = true;
    m_status = VoiceConnectionStatus::CONNECTING;

    // Notify server
    nlohmann::json payload = { {"action", "join"} };
    ClientChat::GetInstance().SendVoiceRoomAction(payload);

    StartAudio();
    m_status = VoiceConnectionStatus::CONNECTED;
    AddToast("Sesli odaya katilindi", ToastType::SUCCESS);
}

void ClientVoice::LeaveRoom() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_inRoom) return;
    m_inRoom = false;
    m_status = VoiceConnectionStatus::DISCONNECTING;

    nlohmann::json payload = { {"action", "leave"} };
    ClientChat::GetInstance().SendVoiceRoomAction(payload);

    DisconnectAllPeers();
    StopAudio();
    m_status = VoiceConnectionStatus::IDLE;
    AddToast("Sesli odadan cikildi", ToastType::INFO);
}

// ---------------------------------------------------------------
// Local audio controls  (Fluxer: microphone.setEnabled / deafen)
// ---------------------------------------------------------------
void ClientVoice::SetMuted(bool muted) {
    m_selfMuted = muted;
    nlohmann::json payload = { {"action", "mute_state"}, {"muted", muted} };
    ClientChat::GetInstance().SendVoiceRoomAction(payload);
}

void ClientVoice::SetDeafened(bool deafened) {
    m_selfDeafened = deafened;
    nlohmann::json payload = { {"action", "deaf_state"}, {"deafened", deafened} };
    ClientChat::GetInstance().SendVoiceRoomAction(payload);
}

// ---------------------------------------------------------------
// Per-participant volume  (Fluxer: participantVolume.set)
// ---------------------------------------------------------------
void ClientVoice::SetParticipantVolume(const std::string& username, float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peers.find(username);
    if (it != m_peers.end()) {
        it->second.volume = std::max(0.f, std::min(2.f, volume));
    }
}

float ClientVoice::GetParticipantVolume(const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peers.find(username);
    return (it != m_peers.end()) ? it->second.volume : 1.0f;
}

// ---------------------------------------------------------------
// Room state getters  (Fluxer: room.participantJoined / Left)
// ---------------------------------------------------------------
std::vector<VoiceParticipant> ClientVoice::GetParticipants() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<VoiceParticipant> result;
    for (const auto& [name, peer] : m_peers) {
        result.push_back(peer);
    }
    return result;
}

size_t ClientVoice::GetParticipantCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_peers.size();
}

// ---------------------------------------------------------------
// Voice room server update handler
// (Fluxer: room.participantJoined / room.participantLeft)
// ---------------------------------------------------------------
void ClientVoice::HandleVoiceRoomUpdate(const nlohmann::json& payload) {
    try {
        std::string action = payload.value("action", "");

        if (action == "participant_joined") {
            std::string who = payload.value("username", "");
            if (who.empty() || who == m_username) return;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_peers.find(who) == m_peers.end()) {
                    VoiceParticipant p;
                    p.username = who;
                    p.incomingBuffer = std::make_shared<AudioBuffer>();
                    p.incomingBuffer->Init(24000 * 2);
                    m_peers[who] = p;
                }
            }
            // We are the "older" peer, initiate WebRTC offer
            if (m_inRoom) {
                ConnectToPeer(who, true);
            }
            AddToast(who + " sesli odaya katildi", ToastType::INFO, 2.f);

        } else if (action == "participant_left") {
            std::string who = payload.value("username", "");
            DisconnectFromPeer(who);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_peers.erase(who);
            }
            AddToast(who + " sesli odadan ayrildi", ToastType::INFO, 2.f);

        } else if (action == "room_state") {
            // Full room participant list (sent on join)
            auto participants = payload.value("participants", nlohmann::json::array());
            for (const auto& p : participants) {
                std::string who = p.value("username", "");
                if (who.empty() || who == m_username) continue;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_peers.find(who) == m_peers.end()) {
                        VoiceParticipant vp;
                        vp.username = who;
                        vp.isMuted    = p.value("muted", false);
                        vp.isDeafened = p.value("deafened", false);
                        vp.incomingBuffer = std::make_shared<AudioBuffer>();
                        vp.incomingBuffer->Init(24000 * 2);
                        m_peers[who] = vp;
                    }
                }
                if (m_inRoom) {
                    ConnectToPeer(who, true);
                }
            }

        } else if (action == "mute_state") {
            std::string who = payload.value("username", "");
            bool muted = payload.value("muted", false);
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_peers.find(who);
            if (it != m_peers.end()) it->second.isMuted = muted;

        } else if (action == "deaf_state") {
            std::string who = payload.value("username", "");
            bool deaf = payload.value("deafened", false);
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_peers.find(who);
            if (it != m_peers.end()) it->second.isDeafened = deaf;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Voice] HandleVoiceRoomUpdate error: " << e.what() << std::endl;
    }
}

// ---------------------------------------------------------------
// Per-peer WebRTC connect (mesh topology)
// ---------------------------------------------------------------
void ClientVoice::ConnectToPeer(const std::string& targetUser, bool isOffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peers.find(targetUser);
    if (it == m_peers.end()) return;
    VoiceParticipant& vp = it->second;

    if (vp.peerConnection) return; // already connecting

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    vp.peerConnection = std::make_shared<rtc::PeerConnection>(config);

    vp.peerConnection->onStateChange([this, targetUser](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it2 = m_peers.find(targetUser);
            if (it2 != m_peers.end()) it2->second.connected = true;
        } else if (state == rtc::PeerConnection::State::Failed ||
                   state == rtc::PeerConnection::State::Closed) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it2 = m_peers.find(targetUser);
            if (it2 != m_peers.end()) it2->second.connected = false;
        }
    });

    vp.peerConnection->onLocalDescription([this, targetUser](rtc::Description desc) {
        nlohmann::json sig = {
            {"target", targetUser},
            {"room_mode", true},
            {"data", {
                {"type", desc.typeString()},
                {"sdp", std::string(desc)}
            }}
        };
        SendSignalingMessage(sig);
    });

    vp.peerConnection->onLocalCandidate([this, targetUser](rtc::Candidate candidate) {
        nlohmann::json sig = {
            {"target", targetUser},
            {"room_mode", true},
            {"data", {
                {"type", "candidate"},
                {"candidate", std::string(candidate)},
                {"mid", candidate.mid()}
            }}
        };
        SendSignalingMessage(sig);
    });

    if (isOffer) {
        rtc::DataChannelInit audioInit;
        audioInit.reliability.unordered    = true;
        audioInit.reliability.maxRetransmits = 0;
        vp.audioChannel = vp.peerConnection->createDataChannel("audio", audioInit);

        auto bufPtr = vp.incomingBuffer;
        auto usernameCapture = targetUser;
        vp.audioChannel->onMessage([this, usernameCapture, bufPtr](rtc::message_variant data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                auto bin = std::get<rtc::binary>(data);
                if (bufPtr) {
                    size_t count = bin.size() / sizeof(int16_t);
                    bufPtr->Write(reinterpret_cast<const int16_t*>(bin.data()), count);
                }
                // speaking detection
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it2 = m_peers.find(usernameCapture);
                if (it2 != m_peers.end()) it2->second.isSpeaking = true;
            }
        });

        vp.peerConnection->setLocalDescription();
    } else {
        auto bufPtr = vp.incomingBuffer;
        auto usernameCapture = targetUser;
        vp.peerConnection->onDataChannel([this, &vp, bufPtr, usernameCapture](std::shared_ptr<rtc::DataChannel> dc) {
            if (dc->label() == "audio") {
                vp.audioChannel = dc;
                dc->onMessage([this, bufPtr, usernameCapture](rtc::message_variant data) {
                    if (std::holds_alternative<rtc::binary>(data)) {
                        auto bin = std::get<rtc::binary>(data);
                        if (bufPtr) {
                            size_t count = bin.size() / sizeof(int16_t);
                            bufPtr->Write(reinterpret_cast<const int16_t*>(bin.data()), count);
                        }
                        std::lock_guard<std::mutex> lock(m_mutex);
                        auto it2 = m_peers.find(usernameCapture);
                        if (it2 != m_peers.end()) it2->second.isSpeaking = true;
                    }
                });
            }
        });
    }
}

void ClientVoice::DisconnectFromPeer(const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peers.find(username);
    if (it != m_peers.end()) {
        if (it->second.audioChannel) it->second.audioChannel->close();
        if (it->second.peerConnection) it->second.peerConnection->close();
        it->second.connected = false;
    }
}

void ClientVoice::DisconnectAllPeers() {
    // Caller must hold m_mutex or call without it — we acquire inside
    for (auto& [name, vp] : m_peers) {
        if (vp.audioChannel) vp.audioChannel->close();
        if (vp.peerConnection) vp.peerConnection->close();
        vp.connected = false;
    }
    m_peers.clear();
}

// ---------------------------------------------------------------
// Signaling handler
// ---------------------------------------------------------------
void ClientVoice::SendSignalingMessage(const nlohmann::json& msg) {
    ClientChat::GetInstance().SendVoiceSignal(msg);
}

void ClientVoice::HandleSignalingMessage(const nlohmann::json& payload) {
    try {
        bool roomMode = payload.value("room_mode", false);

        if (roomMode) {
            // Multi-participant room signaling
            std::string sender   = payload.value("sender", "");
            nlohmann::json data  = payload["data"];
            std::string sigType  = data["type"];

            if (sigType == "offer") {
                ConnectToPeer(sender, false);
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_peers.find(sender);
                if (it != m_peers.end() && it->second.peerConnection) {
                    std::string sdp = data["sdp"];
                    it->second.peerConnection->setRemoteDescription(rtc::Description(sdp, sigType));
                    it->second.peerConnection->setLocalDescription();
                }
            } else if (sigType == "answer") {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_peers.find(sender);
                if (it != m_peers.end() && it->second.peerConnection) {
                    std::string sdp = data["sdp"];
                    it->second.peerConnection->setRemoteDescription(rtc::Description(sdp, sigType));
                }
            } else if (sigType == "candidate") {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_peers.find(sender);
                if (it != m_peers.end() && it->second.peerConnection) {
                    std::string candidateStr = data["candidate"];
                    std::string mid = data["mid"];
                    it->second.peerConnection->addRemoteCandidate(rtc::Candidate(candidateStr, mid));
                }
            }
            return;
        }

        // Legacy 1:1 signaling path
        std::string sender   = payload.value("sender", "");
        nlohmann::json data  = payload["data"];
        std::string sigType  = data["type"];
        std::lock_guard<std::mutex> lock(m_mutex);

        if (sigType == "reject") {
            if (m_peerConnection) { m_peerConnection->close(); m_peerConnection = nullptr; }
            StopCallInternal();
            m_incomingCallPending = false;
            m_incomingCallUser.clear();
            m_pendingOfferPayload = nlohmann::json();
            m_pendingCandidates.clear();
            AddToast("Arama reddedildi veya sonlandirildi.", ToastType::WARNING);
            return;
        }

        if (sigType == "offer") {
            if (m_peerConnection || m_incomingCallPending) {
                nlohmann::json sig = { {"target", sender}, {"data", {{"type", "reject"}}} };
                ClientChat::GetInstance().SendVoiceSignal(sig);
                return;
            }
            m_incomingCallPending = true;
            m_incomingCallUser = sender;
            m_pendingOfferPayload = payload;
            m_pendingCandidates.clear();
            AddToast("Gelen arama: " + sender, ToastType::INFO);
            return;
        }

        if (sigType == "candidate") {
            if (!m_peerConnection) {
                m_pendingCandidates.push_back(data);
            } else {
                std::string c = data["candidate"], mid = data["mid"];
                m_peerConnection->addRemoteCandidate(rtc::Candidate(c, mid));
            }
            return;
        }

        if (m_peerConnection) {
            if (sigType == "answer") {
                std::string sdp = data["sdp"];
                m_peerConnection->setRemoteDescription(rtc::Description(sdp, sigType));
            } else if (sigType == "screen_share_start") {
                m_isReceivingScreen = true;
            } else if (sigType == "screen_share_stop") {
                m_isReceivingScreen = false;
                m_newFrameAvailable = false;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Voice] HandleSignalingMessage error: " << e.what() << std::endl;
    }
}

// ---------------------------------------------------------------
// Audio I/O
// ---------------------------------------------------------------
void ClientVoice::WriteIncomingAudio(const std::string& fromUser, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peers.find(fromUser);
    if (it != m_peers.end() && it->second.incomingBuffer) {
        size_t count = size / sizeof(int16_t);
        it->second.incomingBuffer->Write(reinterpret_cast<const int16_t*>(data), count);
    }
}

void ClientVoice::SendAudioFrameToAll(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [name, vp] : m_peers) {
        if (vp.audioChannel && vp.audioChannel->isOpen() && !vp.isDeafened) {
            vp.audioChannel->send(reinterpret_cast<const std::byte*>(data), size);
        }
    }
    // Also legacy 1:1
    if (m_audioChannel && m_audioChannel->isOpen()) {
        m_audioChannel->send(reinterpret_cast<const std::byte*>(data), size);
    }
}

void ClientVoice::ReadMixedAudioFrame(void* outData, size_t size) {
    size_t count = size / sizeof(int16_t);
    int16_t* out = reinterpret_cast<int16_t*>(outData);
    memset(out, 0, size);

    // Mix all peer buffers (Fluxer: multi-participant audio mixing)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<int16_t> tmp(count);
        for (auto& [name, vp] : m_peers) {
            if (!vp.incomingBuffer) continue;
            vp.incomingBuffer->Read(tmp.data(), count);
            float vol = vp.volume;
            for (size_t i = 0; i < count; ++i) {
                int32_t mixed = (int32_t)out[i] + (int32_t)(tmp[i] * vol);
                out[i] = (int16_t)std::max(-32768, std::min(32767, mixed));
            }
        }
    }
    // Legacy 1:1 mix-in
    {
        std::vector<int16_t> legacy(count);
        m_legacyIncomingBuffer.Read(legacy.data(), count);
        for (size_t i = 0; i < count; ++i) {
            int32_t mixed = (int32_t)out[i] + (int32_t)legacy[i];
            out[i] = (int16_t)std::max(-32768, std::min(32767, mixed));
        }
    }
}

// ---------------------------------------------------------------
// Audio device lifecycle
// ---------------------------------------------------------------
void ClientVoice::StartAudio() {
    std::lock_guard<std::mutex> lock(m_audioMutex);
    if (m_audioDeviceActive) return;

    m_mixBuffer.Init(24000 * 4);
    m_legacyIncomingBuffer.Init(24000 * 2);

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format    = ma_format_s16;
    config.capture.channels  = 1;
    config.playback.format   = ma_format_s16;
    config.playback.channels = 1;
    config.sampleRate        = 24000;
    config.periodSizeInMilliseconds = 20;
    config.dataCallback      = audio_callback;
    config.pUserData         = this;

    if (ma_device_init(NULL, &config, &m_audioDevice) != MA_SUCCESS) {
        std::cerr << "[Audio] Failed to initialize audio device." << std::endl;
        return;
    }
    if (ma_device_start(&m_audioDevice) != MA_SUCCESS) {
        std::cerr << "[Audio] Failed to start audio device." << std::endl;
        ma_device_uninit(&m_audioDevice);
        return;
    }
    m_audioDeviceActive = true;
}

void ClientVoice::StopAudio() {
    std::lock_guard<std::mutex> lock(m_audioMutex);
    if (!m_audioDeviceActive) return;
    ma_device_stop(&m_audioDevice);
    ma_device_uninit(&m_audioDevice);
    m_audioDeviceActive = false;
}

// ---------------------------------------------------------------
// Screen sharing
// ---------------------------------------------------------------
void ClientVoice::SetScreenSharing(bool active) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isScreenSharing == active) return;
        m_isScreenSharing = active;
    }
    if (!m_legacyCallUser.empty()) {
        nlohmann::json sig = {
            {"target", m_legacyCallUser},
            {"data", {{"type", active ? "screen_share_start" : "screen_share_stop"}}}
        };
        SendSignalingMessage(sig);
    }
}

bool ClientVoice::GetNewFrame(std::vector<uint8_t>& outRGBA) {
    std::lock_guard<std::mutex> lock(m_screenMutex);
    if (!m_newFrameAvailable) return false;
    outRGBA = m_screenBuffer;
    m_newFrameAvailable = false;
    return true;
}

void ClientVoice::SendScreenFrame(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_screenChannel && m_screenChannel->isOpen()) {
        m_screenChannel->send(reinterpret_cast<const std::byte*>(data), size);
    }
}

// ---------------------------------------------------------------
// Legacy 1-to-1 call API
// ---------------------------------------------------------------
bool ClientVoice::IsInCall() const {
    return m_peerConnection != nullptr && m_isConnected;
}
bool ClientVoice::IsCalling() const {
    return m_peerConnection != nullptr && !m_isConnected;
}

void ClientVoice::StartCall(const std::string& targetUser) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (IsInCall()) return;
    m_legacyCallUser = targetUser;
    CreatePeerConnection(targetUser, true);
}

void ClientVoice::CreatePeerConnection(const std::string& targetUser, bool isOffer) {
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onStateChange([this, targetUser](rtc::PeerConnection::State state) {
        switch (state) {
            case rtc::PeerConnection::State::Connected:
                m_isConnected = true;
                AddToast("Sesli sohbete katilindi: " + targetUser, ToastType::SUCCESS);
                break;
            case rtc::PeerConnection::State::Disconnected:
            case rtc::PeerConnection::State::Failed:
            case rtc::PeerConnection::State::Closed:
                if (m_isConnected) {
                    m_isConnected = false;
                    AddToast("Sesli sohbet baglantisi koptu.", ToastType::ERR);
                }
                break;
            default: break;
        }
    });

    m_peerConnection->onLocalDescription([this, targetUser](rtc::Description desc) {
        nlohmann::json sig = {
            {"target", targetUser},
            {"data", {{"type", desc.typeString()}, {"sdp", std::string(desc)}}}
        };
        SendSignalingMessage(sig);
    });

    m_peerConnection->onLocalCandidate([this, targetUser](rtc::Candidate candidate) {
        nlohmann::json sig = {
            {"target", targetUser},
            {"data", {{"type", "candidate"}, {"candidate", std::string(candidate)}, {"mid", candidate.mid()}}}
        };
        SendSignalingMessage(sig);
    });

    if (isOffer) {
        rtc::DataChannelInit audioInit;
        audioInit.reliability.unordered     = true;
        audioInit.reliability.maxRetransmits = 0;
        m_audioChannel = m_peerConnection->createDataChannel("audio", audioInit);
        m_screenChannel = m_peerConnection->createDataChannel("screen");

        m_audioChannel->onOpen([this]() { m_isConnected = true; this->StartAudio(); });
        m_audioChannel->onMessage([this](rtc::message_variant data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                auto bin = std::get<rtc::binary>(data);
                size_t count = bin.size() / sizeof(int16_t);
                m_legacyIncomingBuffer.Write(reinterpret_cast<const int16_t*>(bin.data()), count);
            }
        });

        m_screenChannel->onMessage([this](rtc::message_variant data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                auto bin = std::get<rtc::binary>(data);
                size_t numPixels = bin.size() / sizeof(uint16_t);
                if (numPixels == 480 * 270) {
                    const uint16_t* src = reinterpret_cast<const uint16_t*>(bin.data());
                    std::lock_guard<std::mutex> lock(m_screenMutex);
                    m_screenBuffer.resize(480 * 270 * 4);
                    uint8_t* dst = m_screenBuffer.data();
                    for (size_t i = 0; i < numPixels; ++i) {
                        uint16_t pixel = src[i];
                        dst[i*4+0] = ((pixel >> 11) & 0x1F) << 3;
                        dst[i*4+1] = ((pixel >>  5) & 0x3F) << 2;
                        dst[i*4+2] = (pixel & 0x1F) << 3;
                        dst[i*4+3] = 255;
                    }
                    m_newFrameAvailable = true;
                    m_isReceivingScreen = true;
                }
            }
        });
        m_screenChannel->onClosed([this]() { m_isReceivingScreen = false; });

        m_peerConnection->setLocalDescription();
    } else {
        m_peerConnection->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            if (dc->label() == "audio") {
                m_audioChannel = dc;
                m_audioChannel->onOpen([this]() { m_isConnected = true; this->StartAudio(); });
                m_audioChannel->onMessage([this](rtc::message_variant data) {
                    if (std::holds_alternative<rtc::binary>(data)) {
                        auto bin = std::get<rtc::binary>(data);
                        size_t count = bin.size() / sizeof(int16_t);
                        m_legacyIncomingBuffer.Write(reinterpret_cast<const int16_t*>(bin.data()), count);
                    }
                });
            } else if (dc->label() == "screen") {
                m_screenChannel = dc;
                m_screenChannel->onClosed([this]() { m_isReceivingScreen = false; });
                m_screenChannel->onMessage([this](rtc::message_variant data) {
                    if (std::holds_alternative<rtc::binary>(data)) {
                        auto bin = std::get<rtc::binary>(data);
                        size_t numPixels = bin.size() / sizeof(uint16_t);
                        if (numPixels == 480 * 270) {
                            const uint16_t* src = reinterpret_cast<const uint16_t*>(bin.data());
                            std::lock_guard<std::mutex> lock(m_screenMutex);
                            m_screenBuffer.resize(480 * 270 * 4);
                            uint8_t* dst = m_screenBuffer.data();
                            for (size_t i = 0; i < numPixels; ++i) {
                                uint16_t pixel = src[i];
                                dst[i*4+0] = ((pixel >> 11) & 0x1F) << 3;
                                dst[i*4+1] = ((pixel >>  5) & 0x3F) << 2;
                                dst[i*4+2] = (pixel & 0x1F) << 3;
                                dst[i*4+3] = 255;
                            }
                            m_newFrameAvailable = true;
                            m_isReceivingScreen = true;
                        }
                    }
                });
            }
        });
    }
}

void ClientVoice::StopCallInternal() {
    this->StopAudio();
    if (m_audioChannel) { m_audioChannel->close(); m_audioChannel = nullptr; }
    if (m_screenChannel && !m_inRoom) { m_screenChannel->close(); m_screenChannel = nullptr; }
    if (m_peerConnection) {
        if (!m_legacyCallUser.empty()) {
            nlohmann::json sig = {{"target", m_legacyCallUser}, {"data", {{"type", "reject"}}}};
            ClientChat::GetInstance().SendVoiceSignal(sig);
        }
        m_peerConnection->close();
        m_peerConnection = nullptr;
    }
    m_isConnected = false;
    m_isScreenSharing = false;
    m_isReceivingScreen = false;
    m_newFrameAvailable = false;
    m_legacyCallUser.clear();
}

void ClientVoice::StopCall() {
    std::lock_guard<std::mutex> lock(m_mutex);
    StopCallInternal();
}

void ClientVoice::AcceptIncomingCall() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_incomingCallPending) return;
    m_legacyCallUser = m_incomingCallUser;
    CreatePeerConnection(m_incomingCallUser, false);
    try {
        if (m_pendingOfferPayload.contains("data") && m_pendingOfferPayload["data"].contains("sdp")) {
            std::string sdp = m_pendingOfferPayload["data"]["sdp"];
            std::string stype = m_pendingOfferPayload["data"]["type"];
            m_peerConnection->setRemoteDescription(rtc::Description(sdp, stype));
            m_peerConnection->setLocalDescription();
        }
        for (const auto& cand : m_pendingCandidates) {
            std::string c = cand["candidate"], mid = cand["mid"];
            m_peerConnection->addRemoteCandidate(rtc::Candidate(c, mid));
        }
    } catch (const std::exception& e) {
        std::cerr << "[Voice] Exception accepting call: " << e.what() << std::endl;
    }
    m_pendingCandidates.clear();
    m_incomingCallPending = false;
    m_incomingCallUser.clear();
    m_pendingOfferPayload = nlohmann::json();
}

void ClientVoice::RejectIncomingCall() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_incomingCallPending) return;
    nlohmann::json sig = {{"target", m_incomingCallUser}, {"data", {{"type", "reject"}}}};
    SendSignalingMessage(sig);
    m_pendingCandidates.clear();
    m_incomingCallPending = false;
    m_incomingCallUser.clear();
    m_pendingOfferPayload = nlohmann::json();
}
