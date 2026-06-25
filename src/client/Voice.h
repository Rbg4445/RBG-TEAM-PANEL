#ifndef CLIENT_VOICE_H
#define CLIENT_VOICE_H

#include <string>
#include <memory>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#include "miniaudio.h"

// AudioBuffer - ring-buffer with latency control & PLC
struct AudioBuffer {
    std::vector<int16_t> buffer;
    size_t write_pos = 0;
    size_t read_pos  = 0;
    size_t capacity  = 0;
    std::mutex mutex;

    void Init(size_t size) {
        buffer.assign(size, 0);
        write_pos = read_pos = 0;
        capacity = size;
    }

    void Write(const int16_t* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        for (size_t i = 0; i < count; ++i) {
            buffer[write_pos] = data[i];
            write_pos = (write_pos + 1) % capacity;
        }
    }

    void Read(int16_t* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        size_t available = (write_pos >= read_pos)
            ? write_pos - read_pos
            : capacity - read_pos + write_pos;

        // Latency catch-up: >150ms -> skip to ~60ms
        if (available > 3600) {
            size_t skip = available - 1440;
            read_pos = (read_pos + skip) % capacity;
            available = 1440;
        }

        if (available < count) {
            size_t i = 0;
            int16_t last_sample = 0;
            for (; i < available; ++i) {
                data[i] = buffer[read_pos];
                last_sample = data[i];
                read_pos = (read_pos + 1) % capacity;
            }
            float val = last_sample;
            for (; i < count; ++i) { val *= 0.95f; data[i] = (int16_t)val; }
        } else {
            for (size_t i = 0; i < count; ++i) {
                data[i] = buffer[read_pos];
                read_pos = (read_pos + 1) % capacity;
            }
        }
    }
};

// VoiceParticipant - Fluxer: room.participantJoined structure
struct VoiceParticipant {
    std::string username;
    bool isMuted    = false;
    bool isDeafened = false;
    bool isSpeaking = false;
    float volume    = 1.0f;

    std::shared_ptr<rtc::PeerConnection> peerConnection;
    std::shared_ptr<rtc::DataChannel>    audioChannel;
    std::shared_ptr<AudioBuffer>         incomingBuffer;
    bool connected = false;
};

// VoiceConnectionStatus - Fluxer: VoiceEngineV2ConnectionStatus
enum class VoiceConnectionStatus {
    IDLE,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    FAILED
};

class ClientVoice {
public:
    static ClientVoice& GetInstance();

    bool Init();
    void Register(const std::string& username);
    void Close();
    void ConnectSignaler(const std::string&, int = 8080) {}

    // Room join/leave (Fluxer: connection.connect / disconnect)
    void JoinRoom();
    void LeaveRoom();
    bool IsInRoom()  const { return m_inRoom; }
    VoiceConnectionStatus GetStatus() const { return m_status; }

    // Local audio controls (Fluxer: microphone.setEnabled, localAudio.muteRequested)
    void SetMuted(bool muted);
    void SetDeafened(bool deafened);
    bool IsMuted()    const { return m_selfMuted; }
    bool IsDeafened() const { return m_selfDeafened; }

    // Per-participant volume (Fluxer: participantVolume.set)
    void SetParticipantVolume(const std::string& username, float volume);
    float GetParticipantVolume(const std::string& username);

    // Room state (Fluxer: room.participantJoined / Left)
    std::vector<VoiceParticipant> GetParticipants();
    size_t GetParticipantCount();

    // Signaling
    void HandleSignalingMessage(const nlohmann::json& payload);
    void HandleVoiceRoomUpdate(const nlohmann::json& payload);

    // Audio I/O
    void WriteIncomingAudio(const std::string& fromUser, const void* data, size_t size);
    void SendAudioFrameToAll(const void* data, size_t size);
    void ReadMixedAudioFrame(void* data, size_t size);

    void StartAudio();
    void StopAudio();

    // Screen sharing
    void SetScreenSharing(bool active);
    bool IsScreenSharing() const { return m_isScreenSharing; }
    bool IsReceivingScreen() const { return m_isReceivingScreen; }
    bool GetNewFrame(std::vector<uint8_t>& outRGBA);
    void SendScreenFrame(const void* data, size_t size);

    // Legacy 1-to-1 API (backward compat)
    void StartCall(const std::string& targetUser);
    void StopCall();
    bool IsInCall() const;
    bool IsCalling() const;
    const std::string& GetCurrentCallUser() const { return m_legacyCallUser; }
    bool IsIncomingCallPending() const { return m_incomingCallPending; }
    const std::string& GetIncomingCallUser() const { return m_incomingCallUser; }
    void AcceptIncomingCall();
    void RejectIncomingCall();

private:
    ClientVoice() = default;
    ~ClientVoice() = default;
    ClientVoice(const ClientVoice&) = delete;
    ClientVoice& operator=(const ClientVoice&) = delete;

    void SendSignalingMessage(const nlohmann::json& msg);
    void ConnectToPeer(const std::string& targetUser, bool isOffer);
    void DisconnectFromPeer(const std::string& username);
    void DisconnectAllPeers();

    // Legacy helpers
    void CreatePeerConnection(const std::string& targetUser, bool isOffer);
    void StopCallInternal();

    std::string m_username;
    bool m_inRoom       = false;
    bool m_selfMuted    = false;
    bool m_selfDeafened = false;
    VoiceConnectionStatus m_status = VoiceConnectionStatus::IDLE;

    std::map<std::string, VoiceParticipant> m_peers;
    std::mutex m_mutex;

    AudioBuffer m_mixBuffer;

    ma_device m_audioDevice;
    bool m_audioDeviceActive = false;
    std::mutex m_audioMutex;

    bool m_isScreenSharing   = false;
    bool m_isReceivingScreen = false;
    std::vector<uint8_t> m_screenBuffer;
    bool m_newFrameAvailable = false;
    std::mutex m_screenMutex;
    std::shared_ptr<rtc::DataChannel> m_screenChannel;

    // Legacy 1-to-1
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel>    m_audioChannel;
    std::string  m_legacyCallUser;
    bool         m_isConnected        = false;
    bool         m_incomingCallPending = false;
    std::string  m_incomingCallUser;
    nlohmann::json              m_pendingOfferPayload;
    std::vector<nlohmann::json> m_pendingCandidates;
    AudioBuffer  m_legacyIncomingBuffer;
};

#endif // CLIENT_VOICE_H
