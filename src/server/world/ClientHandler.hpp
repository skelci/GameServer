#pragma once

#include "common/TypeUtils.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/thread_pool.hpp>

#include <queue>
#include <mutex>
#include <chrono>
#include <string>
#include <atomic>
#include <vector>
#include <unordered_map>

#define SOCKET std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>

struct UserLogin {
    long uid;
    std::chrono::time_point<std::chrono::steady_clock> time;
};

class ClientHandler {
public:
    static void Init(unsigned short port);
    static void Start();

    static void InitiateShutdown();
    static void Shutdown();

    template<typename... Args>
    static void SendData(SOCKET socket, const Args&... args);

    static SOCKET GetSocketByUID(long uid);
    static long GetUIDBySocket(SOCKET socket);

    static void Disconnect(SOCKET socket, const std::string& reason = "");

    static std::unordered_map<unsigned long, std::string> loggedInUsers;
    static std::mutex loggedInUsersMutex;

private:
    static void AcceptConnections();
    static void RecieveData();
    static void ProcessData();
    static void SendDataFromBuffer();

    static void AddClient(long uid, SOCKET socket);
    static void RemoveClient(long uid);

    static void CheckUnverifiedSockets();

    static void ProcessDataContent(std::string data);

    static boost::asio::io_context io_context;
    static boost::asio::ssl::context ssl_context;
    static boost::asio::ip::tcp::acceptor acceptor;

    static std::vector<SOCKET> clientSockets;
    static std::mutex clientSocketsMutex;

    static std::unordered_map<SOCKET, std::shared_ptr<std::mutex>> clientSocketsMutexes;
    static std::mutex clientSocketsMutexesMutex;
    static std::mutex& GetSocketMutex(SOCKET socket);

    static std::queue<std::string> recieveBuffer;
    static std::mutex recieveBufferMutex;

    static std::queue<TypeUtils::Message> sendBuffer;
    static std::mutex sendBufferMutex;

    static std::unordered_map<long, SOCKET> uidToSocketMap;
    static std::unordered_map<SOCKET, long> socketToUIDMap;
    static std::mutex clientMapsMutex;

    static std::unordered_map<SOCKET, std::chrono::time_point<std::chrono::steady_clock>> unverifiedSockets;
    static std::mutex unverifiedSocketsMutex;

    static std::unordered_map<unsigned short, SOCKET> unverifiedSocketsIDs;
    static std::mutex unverifiedSocketsIDsMutex;
    static unsigned short unverifiedSocketsIDsCounter;

    static std::atomic<bool> running;
    static std::atomic<bool> shutdown;
    static const int maxThreads = 1024;

    static std::mutex disconnectMutex;
    static std::vector<SOCKET> disconnectingSockets;

    static boost::asio::thread_pool threadPool;
};

// ---------------------------- Template functions ---------------------------- //

template<typename... Args>
void ClientHandler::SendData(SOCKET socket, const Args&... args) {
    std::string msg = TypeUtils::stickParams(args...);
    std::lock_guard<std::mutex> sendLock(sendBufferMutex);
    sendBuffer.push({socket, msg});
}
