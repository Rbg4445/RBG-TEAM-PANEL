#ifndef SERVER_VOICE_H
#define SERVER_VOICE_H

// Voice signaling is now routed through the ENet (UDP 7777) chat channel.
// This stub exists only for backward compatibility and is not used.

#include <string>

class ServerVoice {
public:
    static ServerVoice& GetInstance();
    bool Init(int /*port*/ = 8080) { return true; }
    void Close() {}
private:
    ServerVoice() = default;
    ~ServerVoice() = default;
    ServerVoice(const ServerVoice&) = delete;
    ServerVoice& operator=(const ServerVoice&) = delete;
};

#endif // SERVER_VOICE_H
