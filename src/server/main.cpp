#include "DB.h"
#include "Chat.h"
#include "Voice.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

bool g_running = true;

void SignalHandler(int signum) {
    std::cout << "\n[Server] Shutting down..." << std::endl;
    g_running = false;
}

int main() {
    std::cout << "Management Panel Server starting..." << std::endl;

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // 1. Initialize SQLite Database
    if (!DB::GetInstance().Init("management.db")) {
        std::cerr << "[Server] Failed to initialize database." << std::endl;
        return -1;
    }

    // 2. Initialize ENet Server (Port 7777/UDP)
    if (!ServerChat::GetInstance().Init(7777)) {
        std::cerr << "[Server] Failed to initialize ENet server." << std::endl;
        DB::GetInstance().Close();
        return -1;
    }

    // 3. Initialize WebRTC Signaling Server (Port 8080/TCP)
    if (!ServerVoice::GetInstance().Init(8080)) {
        std::cerr << "[Server] Failed to initialize WebRTC signaling server." << std::endl;
        ServerChat::GetInstance().Close();
        DB::GetInstance().Close();
        return -1;
    }

    std::cout << "[Server] All systems running. Press Ctrl+C to terminate." << std::endl;

    // Main Server Loop
    while (g_running) {
        ServerChat::GetInstance().Poll(10); // Service ENet events (10ms timeout)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Cleanup and Shutdown
    ServerVoice::GetInstance().Close();
    ServerChat::GetInstance().Close();
    DB::GetInstance().Close();

    std::cout << "[Server] Shutdown complete." << std::endl;
    return 0;
}
