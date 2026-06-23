#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "Voice.h"
#include "Chat.h"
#include <iostream>

void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ClientVoice* pVoice = (ClientVoice*)pDevice->pUserData;
    if (!pVoice) return;

    if (pInput && pVoice->IsInCall() && !pVoice->IsMuted()) {
        pVoice->SendAudioFrame(pInput, frameCount * sizeof(int16_t));
    }

    if (pOutput) {
        pVoice->ReadAudioFrame(pOutput, frameCount * sizeof(int16_t));
    }
}

enum class ToastType { INFO, SUCCESS, WARNING, ERR };
extern void AddToast(const std::string& msg, ToastType type = ToastType::INFO, float dur = 3.0f);

ClientVoice& ClientVoice::GetInstance() {
    static ClientVoice instance;
    return instance;
}

bool ClientVoice::Init() {
    // Register voice signal callback with ClientChat so packets route here
    ClientChat::GetInstance().SetVoiceSignalCallback([this](const nlohmann::json& payload) {
        HandleSignalingMessage(payload);
    });
    return true;
}

void ClientVoice::Register(const std::string& username) {
    m_username = username;
    // No registration packet needed — identity comes from ENet session
}

void ClientVoice::SendSignalingMessage(const nlohmann::json& msg) {
    ClientChat::GetInstance().SendVoiceSignal(msg);
}

void ClientVoice::CreatePeerConnection(const std::string& targetUser, bool isOffer) {
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onStateChange([this, targetUser](rtc::PeerConnection::State state) {
        switch (state) {
            case rtc::PeerConnection::State::Connecting:
                AddToast("Sese baglaniliyor...", ToastType::INFO);
                break;
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
            {"data", {
                {"type", desc.typeString()},
                {"sdp", std::string(desc)}
            }}
        };
        SendSignalingMessage(sig);
    });

    m_peerConnection->onLocalCandidate([this, targetUser](rtc::Candidate candidate) {
        nlohmann::json sig = {
            {"target", targetUser},
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
        audioInit.reliability.unordered = true;
        audioInit.reliability.maxRetransmits = 0;
        m_audioChannel = m_peerConnection->createDataChannel("audio", audioInit);
        m_screenChannel = m_peerConnection->createDataChannel("screen");

        m_audioChannel->onOpen([this]() {
            m_isConnected = true;
            this->StartAudio();
        });

        m_audioChannel->onMessage([this](rtc::message_variant data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                auto bin = std::get<rtc::binary>(data);
                this->WriteIncomingAudio(bin.data(), bin.size());
            }
        });

        m_screenChannel->onOpen([this]() {
            // Screen channel open
        });

        m_screenChannel->onClosed([this]() {
            m_isReceivingScreen = false;
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
                        dst[i*4 + 0] = ((pixel >> 11) & 0x1F) << 3; // R
                        dst[i*4 + 1] = ((pixel >> 5) & 0x3F) << 2;  // G
                        dst[i*4 + 2] = (pixel & 0x1F) << 3;         // B
                        dst[i*4 + 3] = 255;                         // A
                    }
                    m_newFrameAvailable = true;
                    m_isReceivingScreen = true;
                }
            }
        });

        m_peerConnection->setLocalDescription();
    } else {
        m_peerConnection->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            if (dc->label() == "audio") {
                m_audioChannel = dc;

                m_audioChannel->onOpen([this]() {
                    m_isConnected = true;
                    this->StartAudio();
                });

                m_audioChannel->onMessage([this](rtc::message_variant data) {
                    if (std::holds_alternative<rtc::binary>(data)) {
                        auto bin = std::get<rtc::binary>(data);
                        this->WriteIncomingAudio(bin.data(), bin.size());
                    }
                });
            } else if (dc->label() == "screen") {
                m_screenChannel = dc;
                
                m_screenChannel->onClosed([this]() {
                    m_isReceivingScreen = false;
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
                                dst[i*4 + 0] = ((pixel >> 11) & 0x1F) << 3; // R
                                dst[i*4 + 1] = ((pixel >> 5) & 0x3F) << 2;  // G
                                dst[i*4 + 2] = (pixel & 0x1F) << 3;         // B
                                dst[i*4 + 3] = 255;                         // A
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

void ClientVoice::StartCall(const std::string& targetUser) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (IsInCall()) return;
    m_currentCallUser = targetUser;
    CreatePeerConnection(targetUser, true);
}

void ClientVoice::StopCallInternal() {
    this->StopAudio();
    if (m_audioChannel) { m_audioChannel->close(); m_audioChannel = nullptr; }
    if (m_screenChannel) { m_screenChannel->close(); m_screenChannel = nullptr; }
    if (m_peerConnection) {
        if (!m_currentCallUser.empty()) {
            nlohmann::json sig = {
                {"target", m_currentCallUser},
                {"data", {
                    {"type", "reject"}
                }}
            };
            ClientChat::GetInstance().SendVoiceSignal(sig);
        }
        m_peerConnection->close();
        m_peerConnection = nullptr;
    }
    m_isConnected = false;
    m_isScreenSharing = false;
    m_isReceivingScreen = false;
    m_newFrameAvailable = false;
    m_currentCallUser.clear();
}

void ClientVoice::StopCall() {
    std::lock_guard<std::mutex> lock(m_mutex);
    StopCallInternal();
}

void ClientVoice::Close() {
    StopCall();
}

void ClientVoice::AcceptIncomingCall() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_incomingCallPending) return;

    m_currentCallUser = m_incomingCallUser;
    CreatePeerConnection(m_incomingCallUser, false);

    try {
        if (m_pendingOfferPayload.contains("data") && m_pendingOfferPayload["data"].contains("sdp")) {
            std::string sdp = m_pendingOfferPayload["data"]["sdp"];
            std::string signalType = m_pendingOfferPayload["data"]["type"];
            m_peerConnection->setRemoteDescription(rtc::Description(sdp, signalType));
            m_peerConnection->setLocalDescription();
        }

        // Apply cached candidates
        for (const auto& cand : m_pendingCandidates) {
            std::string candidateStr = cand["candidate"];
            std::string mid = cand["mid"];
            m_peerConnection->addRemoteCandidate(rtc::Candidate(candidateStr, mid));
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

    nlohmann::json sig = {
        {"target", m_incomingCallUser},
        {"data", {
            {"type", "reject"}
        }}
    };
    SendSignalingMessage(sig);

    m_pendingCandidates.clear();
    m_incomingCallPending = false;
    m_incomingCallUser.clear();
    m_pendingOfferPayload = nlohmann::json();
}

void ClientVoice::HandleSignalingMessage(const nlohmann::json& payload) {
    try {
        std::string sender = payload.value("sender", "");
        nlohmann::json data = payload["data"];
        std::string signalType = data["type"];

        std::lock_guard<std::mutex> lock(m_mutex);

        if (signalType == "reject") {
            if (m_peerConnection) {
                m_peerConnection->close();
                m_peerConnection = nullptr;
            }
            StopCallInternal();
            m_pendingCandidates.clear();
            m_incomingCallPending = false;
            m_incomingCallUser.clear();
            m_pendingOfferPayload = nlohmann::json();
            AddToast("Arama reddedildi veya sonlandirildi.", ToastType::WARNING);
            return;
        }

        if (signalType == "offer") {
            if (m_peerConnection || m_incomingCallPending) {
                // Busy: Automatically send a reject signal
                nlohmann::json sig = {
                    {"target", sender},
                    {"data", {
                        {"type", "reject"}
                    }}
                };
                // Can't use SendSignalingMessage here directly if it locks,
                // but SendSignalingMessage doesn't lock m_mutex itself.
                // However, ClientChat::SendVoiceSignal does not lock m_mutex, so it's safe.
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

        if (signalType == "candidate") {
            if (!m_peerConnection) {
                m_pendingCandidates.push_back(data);
            } else {
                std::string candidateStr = data["candidate"];
                std::string mid = data["mid"];
                m_peerConnection->addRemoteCandidate(rtc::Candidate(candidateStr, mid));
            }
            return;
        }

        if (m_peerConnection) {
            if (signalType == "answer") {
                std::string sdp = data["sdp"];
                m_peerConnection->setRemoteDescription(rtc::Description(sdp, signalType));
            } else if (signalType == "screen_share_start") {
                m_isReceivingScreen = true;
            } else if (signalType == "screen_share_stop") {
                m_isReceivingScreen = false;
                m_newFrameAvailable = false;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Voice] Error handling signal: " << e.what() << std::endl;
    }
}

void ClientVoice::WriteIncomingAudio(const void* data, size_t size) {
    size_t count = size / sizeof(int16_t);
    m_incomingBuffer.Write(reinterpret_cast<const int16_t*>(data), count);
}

void ClientVoice::SendAudioFrame(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_audioChannel && m_audioChannel->isOpen()) {
        m_audioChannel->send(reinterpret_cast<const std::byte*>(data), size);
    }
}

void ClientVoice::ReadAudioFrame(void* data, size_t size) {
    size_t count = size / sizeof(int16_t);
    m_incomingBuffer.Read(reinterpret_cast<int16_t*>(data), count);
}

void ClientVoice::StartAudio() {
    std::lock_guard<std::mutex> lock(m_audioMutex);
    if (m_audioDeviceActive) return;

    m_incomingBuffer.Init(24000 * 2);

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format   = ma_format_s16;
    config.capture.channels = 1;
    config.playback.format  = ma_format_s16;
    config.playback.channels = 1;
    config.sampleRate       = 24000;
    config.periodSizeInMilliseconds = 20;
    config.dataCallback     = audio_callback;
    config.pUserData        = this;

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

void ClientVoice::SetScreenSharing(bool active) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isScreenSharing == active) return;
        m_isScreenSharing = active;
    }

    if (IsInCall() && !m_currentCallUser.empty()) {
        nlohmann::json sig = {
            {"target", m_currentCallUser},
            {"data", {
                {"type", active ? "screen_share_start" : "screen_share_stop"}
            }}
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
