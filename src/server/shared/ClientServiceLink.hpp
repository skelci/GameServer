#pragma once

#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <functional>

#include "common/TypeUtils.hpp"


class ClientServiceLink {
public:
    static void StartClient(const std::string& dir);
    static void DisconnectFromTcpServer();

    static void SetMessageHandler(std::function<void(const std::string&)> handler);

    template<typename... Args>
    static void SendData(const Args&... args);

    static void Log(const std::string& message, const short level = 0);

    static std::atomic<bool> running;
    
private:
    static void ProcessMessages();
    static void ProcessSendBuffer();
    static bool ConnectToTcpServer(const std::string& ip, int port);
    static void HandleConnection();
    static bool SendDataFromBuffer(const std::string& message);

    static void HandleMessageContent(const std::string& message);

    static int sock;
    static std::mutex sockMutex;
    static std::vector<std::string> messageBuffer;
    static std::mutex bufferMutex;
    static std::vector<std::string> sendBuffer;
    static std::mutex sendBufferMutex;
    static int serviceId;
    static std::function<void(const std::string&)> messageHandler;
};

// ---------------------------- Template functions ---------------------------- //

template<typename... Args>
void ClientServiceLink::SendData(const Args&... args) {
    std::string msg = TypeUtils::stickParams(serviceId, args..., (char)4);
    std::lock_guard<std::mutex> lock(sendBufferMutex);
    sendBuffer.push_back(msg);
}
