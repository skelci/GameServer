#include "ClientServiceLink.hpp"

#include "SettingsManager.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
#endif
#include <thread>
#include <sstream>
#include <iostream>
#include <stdexcept>

int ClientServiceLink::sock = 0;
std::mutex ClientServiceLink::sockMutex;
std::vector<std::string> ClientServiceLink::messageBuffer;
std::mutex ClientServiceLink::bufferMutex;
std::vector<std::string> ClientServiceLink::sendBuffer;
std::mutex ClientServiceLink::sendBufferMutex;
int ClientServiceLink::serviceId = 0;
std::function<void(const std::string&)> ClientServiceLink::messageHandler = nullptr;
std::atomic<bool> ClientServiceLink::running = true;


bool ClientServiceLink::ConnectToTcpServer(const std::string& ip, int port) {
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            throw std::runtime_error("Winsock initialization failed");
        }
    #endif

    {
        std::lock_guard<std::mutex> lock(sockMutex);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            throw std::runtime_error("Error creating socket");
        }
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr);

    std::lock_guard<std::mutex> lock(sockMutex);
    if (connect(sock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Connection failed\n";
        return false;
    }

    return true;
}

void ClientServiceLink::DisconnectFromTcpServer() {
    std::lock_guard<std::mutex> lock(sockMutex);
    #ifdef _WIN32
        closesocket(sock);
        WSACleanup();
    #else
        close(sock);
    #endif
    sock = 0;
}

void ClientServiceLink::HandleConnection() {
    const int bufferSize = 1024;
    char buffer[bufferSize];
    std::string tempBuffer;

    int sock_;
    {
        std::lock_guard<std::mutex> lock(sockMutex);
        sock_ = sock;
    }

    #ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock_, FIONBIO, &mode);
    #else
        int flags = fcntl(sock_, F_GETFL, 0);
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    #endif

    while (running) {
        memset(buffer, 0, bufferSize);

        int bytesReceived = recv(sock_, buffer, bufferSize, 0);

        if (bytesReceived < 0) {
            #ifdef _WIN32
                int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) {
                    if (!running) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                } else {
                    std::cerr << "Error receiving message from server\n";
                    return;
                }
            #else
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    if (!running) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                } else {
                    std::cerr << "Error receiving message from server\n";
                    return;
                }
            #endif
        } else if (bytesReceived == 0) {
            std::cerr << "Connection closed by server\n";
            return;
        }

        tempBuffer.append(buffer, bytesReceived);

        size_t pos;
        while ((pos = tempBuffer.find(static_cast<char>(4))) != std::string::npos) {
            std::string receivedMessage = tempBuffer.substr(0, pos);
            tempBuffer.erase(0, pos + 1);

            {
                std::lock_guard<std::mutex> lock(bufferMutex);
                messageBuffer.push_back(receivedMessage);
            }

            #ifdef DEBUG
                std::cout << "Received message: " << receivedMessage << std::endl;
            #endif
        }
    }

    #ifdef _WIN32
        mode = 0;
        ioctlsocket(sock_, FIONBIO, &mode);
    #else
        fcntl(sock_, F_SETFL, flags);
    #endif
}

void ClientServiceLink::ProcessSendBuffer() {
    while (running) {
        int sock_;
        {
            std::lock_guard<std::mutex> sockLock(sockMutex);
            sock_ = sock;
        }
        std::unique_lock<std::mutex> bufferLock(sendBufferMutex);
        if (!sendBuffer.empty() && sock_ > 0) {
std::cerr << "Sent message0: " << std::endl;
            std::string message = sendBuffer.front();
            bufferLock.unlock();
            if (SendDataFromBuffer(message)) {
                bufferLock.lock();
                sendBuffer.erase(sendBuffer.begin());
                bufferLock.unlock();
            }

        } else {
            bufferLock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool ClientServiceLink::SendDataFromBuffer(const std::string& message) {
    int sock_;
    {
        std::lock_guard<std::mutex> lock(sockMutex);
        sock_ = sock;
    }
    if (sock_ <= 0) {
        std::cerr << "Socket is not connected\n";
        return false;
    }

    ssize_t bytesSent = send(sock_, message.c_str(), message.length(), 0);
    if (bytesSent == -1) {
        std::cerr << "Failed to send message to the service.\n";
        return false;
    }
std::cerr << "Sent message: " << message << std::endl;
    return true;
}

void ClientServiceLink::StartClient(const std::string& dir) {
    SettingsManager::LoadSettings(dir + "/config.json");

    serviceId = SettingsManager::GetSettings().serviceId;

    std::thread messageThread(ProcessMessages);
    std::thread sendThread(ProcessSendBuffer);

    std::cout << "Service link client started\n";

    while (running) {
        if (!ConnectToTcpServer(SettingsManager::GetSettings().ip, SettingsManager::GetSettings().port)) {
            std::cerr << "Can not connect to server. Retrying in 5 seconds...\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        SendData("CONNECT");

        HandleConnection();

        DisconnectFromTcpServer();
        std::cerr << "Disconnected from serice link server.\n";
        if (!running) break;
    }

    messageThread.join();
    sendThread.join();
}

void ClientServiceLink::ProcessMessages() {
    while (true) {
        std::unique_lock<std::mutex> lock(bufferMutex);
        if (!messageBuffer.empty()) {
            std::string msg = messageBuffer.front();
            messageBuffer.erase(messageBuffer.begin());
            lock.unlock();

            HandleMessageContent(msg);
        } else {
            lock.unlock();
            if (!running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ClientServiceLink::Log(const std::string& message, const short level) {
    if (level > 0) std::cout << message << std::endl;
    ClientServiceLink::SendData("LOG", level, message);
}

void ClientServiceLink::SetMessageHandler(std::function<void(const std::string&)> handler) {
    messageHandler = handler;
}

void ClientServiceLink::HandleMessageContent(const std::string& msg) {
    if (messageHandler) {
        messageHandler(msg);
    }
}
