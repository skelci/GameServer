#include "ServiceLink.hpp"

#include "Log.hpp"
#include "AdminConsole.hpp"

#ifdef _WIN32
	#include <winsock2.h>
	#include <Ws2tcpip.h>
	#pragma comment(lib, "Ws2_32.lib")
#else
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <sys/select.h>
	#include <netinet/in.h>
#endif
#include <thread>
#include <chrono>
#include <unistd.h>

std::mutex ServiceLink::connectionMutex;
std::condition_variable ServiceLink::connectionCond;
int ServiceLink::activeConnections = 0;
std::array<int, MAX_CONNECTIONS> ServiceLink::serviceSockets = {-1, -1, -1, -1};
std::mutex ServiceLink::socketMutex;
std::queue<Message> ServiceLink::messageBuffer;
std::mutex ServiceLink::bufferMutex;
std::array<std::queue<std::string>, MAX_CONNECTIONS> ServiceLink::sendBuffer;
std::mutex ServiceLink::sendBufferMutex;

std::unordered_map<short, std::string> ServiceLink::serviceNames = {
    {0, "Voice"},
    {1, "Chat"},
    {2, "World"},
    {3, "Auth"}
};

std::unordered_map<std::string, short> ServiceLink::serviceIds = {
    {"Voice", 0},
    {"Chat", 1},
    {"World", 2},
    {"Auth", 3}
};

bool ServiceLink::SendDataFromBuffer(int serviceId, const std::string& message) {
    int socket;
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        socket = serviceSockets[serviceId];
    }

    if (socket <= 0) {
        return false;
    }
    ssize_t bytesSent = send(socket, message.c_str(), message.length(), 0);
    if (bytesSent == -1) {
        Log::Print("Failed to send message to service with id " + std::to_string(serviceId) + ".", 3);
        return false;
    }
    return true;
}

void ServiceLink::ProcessSendBuffer() {
    bool isBufferEmpty = false;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            if (!AdminConsole::isRunning && (activeConnections == 0 || isBufferEmpty)) {
                break;
            }
        }
        bool wasNewMessage = false;
        std::unique_lock<std::mutex> bufferLock(sendBufferMutex);
        bufferLock.unlock();
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            std::unique_lock<std::mutex> socketLock(socketMutex);
            bufferLock.lock();
            if (auto socket = serviceSockets[i] > 0 && !sendBuffer[i].empty()) {
                socketLock.unlock();
                if (socket == -1) {
                    std::queue<std::string> emptyQueue;
                    std::swap(sendBuffer[i], emptyQueue);
                } else {
                    wasNewMessage = true;
                    std::string message = sendBuffer[i].front();
                    bufferLock.unlock();

                    if (SendDataFromBuffer(i, message)) {
                        bufferLock.lock();
                        sendBuffer[i].pop();
                    }
                }
            }
            bufferLock.unlock();
        }
        if (!wasNewMessage) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            bufferLock.lock();
            isBufferEmpty = sendBuffer[0].empty() && sendBuffer[1].empty() && sendBuffer[2].empty() && sendBuffer[3].empty();
            bufferLock.unlock();
        }
    }
}

void ServiceLink::HandleConnection(int socket) {
    char buffer[1024];
    std::string tempBuffer;
    int serviceId = -1;
    bool validConnection = true;

    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        activeConnections++;
    }

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(socket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            break;
        }

        tempBuffer.append(buffer, bytesReceived);

        size_t pos;
        while ((pos = tempBuffer.find(static_cast<char>(4))) != std::string::npos) {
            std::string receivedMessage = tempBuffer.substr(0, pos);
            tempBuffer.erase(0, pos + 1);

            serviceId = stoi(TypeUtils::getFirstParam(receivedMessage));

            if (receivedMessage == "CONNECT\036") {
                receivedMessage += std::to_string(socket);

                {
                    std::lock_guard<std::mutex> lock(socketMutex);
                    if (serviceSockets[serviceId] > 0) {
                        validConnection = false;
                        Log::Print("Service " + serviceNames[serviceId] + " already connected", 4);
                        return;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> bufferLock(bufferMutex);
                messageBuffer.push({serviceId, receivedMessage});
            }
        }
    }

    if ((serviceId != -1) == validConnection) {
        std::lock_guard<std::mutex> bufferLock(bufferMutex);
        messageBuffer.push({serviceId, "DISCONNECT"});
    }
    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        activeConnections--;
    }

    connectionCond.notify_one();

    #ifdef _WIN32
        closesocket(socket);
    #else
        close(socket);
    #endif
}

void ServiceLink::StartTcpServer(int port) {
    #ifdef _WIN32
        WSADATA wsaData;
        SOCKET server_fd, new_socket;
    #else
        int server_fd, new_socket;
    #endif
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    #ifdef _WIN32
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Log::Print("WSAStartup failed.", 3);
            exit(EXIT_FAILURE);
        }
    #endif

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(AdvancedSettingsManager::GetSettings().controlServiceIp.c_str());
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    Log::Print("ServiceLink server is listening on port " + std::to_string(port), 1);

    while (true) {
        {
            std::unique_lock<std::mutex> lock(connectionMutex);
            connectionCond.wait(lock, []{ return AdminConsole::isShuttingDown || activeConnections < MAX_CONNECTIONS; });
        }

        if (AdminConsole::isShuttingDown) {
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1; // 1 second timeout
        timeout.tv_usec = 0;

        int activity = select(server_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0 && errno != EINTR) {
            perror("select error");
        }

        if (activity > 0 && FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            std::thread t(HandleConnection, new_socket);
            t.detach();
        }

        if (AdminConsole::isShuttingDown) {
            break;
        }
    }

    #ifdef _WIN32
        closesocket(server_fd);
        WSACleanup();
    #else
        close(server_fd);
    #endif
}

void ServiceLink::NotifyConnection() {
    std::unique_lock<std::mutex> lock(connectionMutex);
    connectionCond.notify_all();
}

void ServiceLink::ProcessMessages() {
    bool messageBufferEmpty = false;
    while (AdminConsole::isRunning || !messageBufferEmpty) {
        std::unique_lock<std::mutex> lock(bufferMutex);
        messageBufferEmpty = messageBuffer.empty();
        if (!messageBufferEmpty) {
            Message msg = messageBuffer.front();
            messageBuffer.pop();
            lock.unlock();

            HandleMessageContent(msg);
        } else {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ServiceLink::HandleMessageContent(Message msg) {
    int serviceId = msg.serviceId;
    std::string action = TypeUtils::getFirstParam(msg.content);
    std::string content = msg.content;
    auto settings = AdvancedSettingsManager::GetSettings();

    if (action == "CONNECT") {
        Log::Print("Service " + std::to_string(serviceId) + " connected", 1);
        std::lock_guard<std::mutex> lock(socketMutex);
        serviceSockets[serviceId] = stoi(TypeUtils::getFirstParam(content));

        if (serviceNames[serviceId] == "Auth") {
            std::string dbConnString = "host=" + settings.dbhostaddr + 
                                       " port=" + std::to_string(settings.dbport) + 
                                       " dbname=" + settings.dbname + 
                                       " user=" + settings.dbuser + 
                                       " password=" + settings.dbpassword;

            SendData(serviceId, "SETTING_SET_dbConnString", dbConnString);
            SendData(serviceId, "SETTING_SET_port", std::to_string(settings.authServicePort));
            SendData(serviceId, "SETTING_SET_emailVerificationsAttempts", std::to_string(settings.emailVerificationsAttempts));
            SendData(serviceId, "SETTING_SET_loginAttempts", std::to_string(settings.loginAttempts));
            SendData(serviceId, "SETTING_SET_loginTime", std::to_string(settings.loginTime));
            SendData(serviceId, "SETTING_SET_emailVerificationTime", std::to_string(settings.emailVerificationTime));
        } else if (serviceNames[serviceId] == "World") {
            SendData(serviceId, "SETTING_SET_port", std::to_string(settings.worldServicePort));
        }

    } else if (action == "DISCONNECT") {
        Log::Print("Service " + std::to_string(serviceId) + " disconnected", 2);
        std::lock_guard<std::mutex> lock(socketMutex);
        serviceSockets[serviceId] = -1;

    } else if (action == "LOG") {
        int level = stoi(TypeUtils::getFirstParam(content));
        std::string log = TypeUtils::getFirstParam(content);
        Log::Print("["+serviceNames[serviceId]+"] " + log, level);

    } else if (action == "LOGIN") {
        SendData(serviceIds["World"], "LOGIN", content);

    } else {
        Log::Print("Unknown action: " + action, 4);
    }
}
