#pragma once

#include "common/TypeUtils.hpp"

#include <array>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <condition_variable>

#define MAX_CONNECTIONS 4


struct Message {
    int serviceId;
    std::string content;
};

class ServiceLink {
public:
	static void StartTcpServer(int port);
	static void ProcessMessages();
	static void ProcessSendBuffer();
	
	template<typename... Args>
    static void SendData(int serviceId, const Args&... args);

	static void NotifyConnection();

private:
	static void HandleConnection(int socket);
	static void HandleMessageContent(Message msg);
	static bool SendDataFromBuffer(int serviceId, const std::string& message);

	static std::unordered_map<short, std::string> serviceNames;

	static std::mutex connectionMutex;
	static std::condition_variable connectionCond;
	static int activeConnections;

	static std::array<int, MAX_CONNECTIONS> serviceSockets;
	static std::mutex socketMutex;
	static std::queue<Message> messageBuffer;
	static std::mutex bufferMutex;
	static std::array<std::queue<std::string>, MAX_CONNECTIONS> sendBuffer;
	static std::mutex sendBufferMutex;
};

// ---------------------------- Template functions ---------------------------- //

template<typename... Args>
void ServiceLink::SendData(int serviceId, const Args&... args) {
    std::string msg = TypeUtils::stickParams(args..., (char)4);
    std::lock_guard<std::mutex> lock(sendBufferMutex);
    sendBuffer[serviceId].push(msg);
}
