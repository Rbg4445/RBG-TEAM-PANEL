#ifndef CLIENT_VOICE_H
#define CLIENT_VOICE_H

#include <string>
#include <memory>
#include <mutex>
#include <map>
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#include "miniaudio.h"

struct AudioBuffer {
    std::vector<int16_t> buffer;
    size_t write_pos = 0;
    size_t read_pos = 0;
    size_t capacity = 0;
    std::mutex mutex;

    void Init(size_t size) {
        buffer.assign(size, 0);
        write_pos = 0;
        read_pos = 0;
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
        size_t available = 0;
        if (write_pos >= read_pos) {
            available = write_pos - read_pos;
        } else {
            available = capacity - read_pos + write_pos;
        }

        if (available < count) {
            size_t i = 0;
            for (; i < available; ++i) {
                data[i] = buffer[read_pos];
                read_pos = (read_pos + 1) % capacity;
            }
            for (; i < count; ++i) {
                data[i] = 0;
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                data[i] = buffer[read_pos];
                read_pos = (read_pos + 1) % capacity;
            }
        }
    }
};

class ClientVoice {
public:
    static ClientVoice& GetInstance();

    bool Init();
    // ConnectSignaler is now a no-op — signaling routes through ENet
    void ConnectSignaler(const std::string& /*hostIp*/, int /*port*/ = 8080) {}
    void Register(const std::string& username);
    void StartCall(const std::string& targetUser);
    void StopCall();
    void Close();

    void AcceptIncomingCall();
    void RejectIncomingCall();
    bool IsIncomingCallPending() const { return m_incomingCallPending; }
    const std::string& GetIncomingCallUser() const { return m_incomingCallUser; }

    void SetMuted(bool muted) { m_isMuted = muted; }
    bool IsMuted() const { return m_isMuted; }
    bool IsInCall() const { return m_peerConnection != nullptr && m_isConnected; }
    bool IsCalling() const { return m_peerConnection != nullptr && !m_isConnected; }
    const std::string& GetCurrentCallUser() const { return m_currentCallUser; }

    // Screen sharing
    void SetScreenSharing(bool active);
    bool IsScreenSharing() const { return m_isScreenSharing; }
    bool IsReceivingScreen() const { return m_isReceivingScreen; }
    bool GetNewFrame(std::vector<uint8_t>& outRGBA); // Returns true if a new frame was copied
    void SendScreenFrame(const void* data, size_t size);

    void WriteIncomingAudio(const void* data, size_t size);
    void SendAudioFrame(const void* data, size_t size);
    void ReadAudioFrame(void* data, size_t size);

    void StartAudio();
    void StopAudio();

    // Called by ClientChat when a voice_signal packet arrives
    void HandleSignalingMessage(const nlohmann::json& payload);

private:
    ClientVoice() = default;
    ~ClientVoice() = default;
    ClientVoice(const ClientVoice&) = delete;
    ClientVoice& operator=(const ClientVoice&) = delete;

    void CreatePeerConnection(const std::string& targetUser, bool isOffer);
    void SendSignalingMessage(const nlohmann::json& msg);
    void StopCallInternal();

    std::shared_ptr<rtc::PeerConnection> m_peerConnection = nullptr;
    std::shared_ptr<rtc::DataChannel> m_audioChannel = nullptr;
    std::shared_ptr<rtc::DataChannel> m_screenChannel = nullptr;

    std::string m_username;
    std::string m_currentCallUser;
    bool m_isConnected = false;
    bool m_isMuted = false;

    // Incoming call pending states
    bool m_incomingCallPending = false;
    std::string m_incomingCallUser;
    nlohmann::json m_pendingOfferPayload;
    std::vector<nlohmann::json> m_pendingCandidates;
    
    // Screen sharing states
    bool m_isScreenSharing = false;
    bool m_isReceivingScreen = false;
    std::vector<uint8_t> m_screenBuffer; // Stores 480x270x4 RGBA pixels
    bool m_newFrameAvailable = false;
    std::mutex m_screenMutex;
    std::mutex m_mutex;

    ma_device m_audioDevice;
    bool m_audioDeviceActive = false;
    std::mutex m_audioMutex;
    AudioBuffer m_incomingBuffer;
};

#endif // CLIENT_VOICE_H
