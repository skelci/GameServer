#include "ClientHandler.hpp"

#include "defines.hpp"
#include "shared/ClientServiceLink.hpp"

#include <boost/asio/post.hpp>

#include <thread>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

#ifdef DEBUG
#include <iostream>
#endif

std::unordered_map<unsigned long, std::string> ClientHandler::loggedInUsers;
std::mutex ClientHandler::loggedInUsersMutex;

boost::asio::io_context ClientHandler::io_context;
boost::asio::ssl::context ClientHandler::ssl_context(boost::asio::ssl::context::tlsv13);
boost::asio::ip::tcp::acceptor ClientHandler::acceptor(io_context);

std::vector<SOCKET> ClientHandler::clientSockets;
std::mutex ClientHandler::clientSocketsMutex;

std::unordered_map<SOCKET, std::shared_ptr<std::mutex>> ClientHandler::clientSocketsMutexes;
std::mutex ClientHandler::clientSocketsMutexesMutex;

std::queue<std::string> ClientHandler::recieveBuffer;
std::mutex ClientHandler::recieveBufferMutex;

std::queue<TypeUtils::Message> ClientHandler::sendBuffer;
std::mutex ClientHandler::sendBufferMutex;

std::unordered_map<long, SOCKET> ClientHandler::uidToSocketMap;
std::unordered_map<SOCKET, long> ClientHandler::socketToUIDMap;
std::mutex ClientHandler::clientMapsMutex;

std::unordered_map<SOCKET, std::chrono::time_point<std::chrono::steady_clock>> ClientHandler::unverifiedSockets;
std::mutex ClientHandler::unverifiedSocketsMutex;

std::unordered_map<unsigned short, SOCKET> ClientHandler::unverifiedSocketsIDs;
std::mutex ClientHandler::unverifiedSocketsIDsMutex;
unsigned short ClientHandler::unverifiedSocketsIDsCounter;

std::atomic<bool> ClientHandler::running(true);
std::atomic<bool> ClientHandler::shutdown(false);

std::mutex ClientHandler::disconnectMutex;
std::vector<SOCKET> ClientHandler::disconnectingSockets;

boost::asio::thread_pool ClientHandler::threadPool(std::thread::hardware_concurrency());

void ClientHandler::Init(unsigned short port) {
    const std::string certFile = DIR + "auth/network/server.crt";
    const std::string keyFile = DIR + "auth/network/server.key";

    if (!std::filesystem::exists(certFile) || !std::filesystem::exists(keyFile)) {
        throw std::runtime_error("Certificate or key file not found");
    }

    ssl_context.use_certificate_chain_file(certFile);
    ssl_context.use_private_key_file(keyFile, boost::asio::ssl::context::pem);

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
    acceptor.open(endpoint.protocol());
    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen();
}

void ClientHandler::Start() {
    std::thread(&ClientHandler::AcceptConnections).detach();
    std::thread(&ClientHandler::RecieveData).detach();
    std::thread(&ClientHandler::ProcessData).detach();
    std::thread(&ClientHandler::SendDataFromBuffer).detach();

    ClientServiceLink::Log("Running io context...");
    for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
        std::thread([]() {
            while (!shutdown) {
                io_context.run();
                io_context.restart();
            }
        }).detach();
    }

    std::thread(&ClientHandler::CheckUnverifiedSockets).detach();
}

void ClientHandler::InitiateShutdown() {
    running = false;
}

void ClientHandler::Shutdown() {
    std::lock_guard<std::mutex> lock(clientSocketsMutex);
    shutdown = true;

    {
        for (auto& socket : clientSockets) {
            Disconnect(socket, "Server has been shut down.");
        }
    }

    io_context.stop();
    ClientServiceLink::Log("Server has been shut down.", 1);
}

void ClientHandler::AcceptConnections() {
    if (!running) return;

    auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(io_context, ssl_context);
    acceptor.async_accept(socket->lowest_layer(), [socket](const boost::system::error_code& error) {
        if (!running) return;

        if (!error) {
            socket->async_handshake(boost::asio::ssl::stream_base::server, [socket](const boost::system::error_code& error) {
                if (!running) return;

                if (!error) {
                    {
                        std::lock_guard<std::mutex> lock(unverifiedSocketsMutex);
                        unverifiedSockets[socket] = std::chrono::steady_clock::now();
                    }
                    {
                        std::lock_guard<std::mutex> lock(unverifiedSocketsIDsMutex);
                        unverifiedSocketsIDs[unverifiedSocketsIDsCounter++] = socket;
                    }
                    {
                        std::lock_guard<std::mutex> lock(clientSocketsMutexesMutex);
                        clientSocketsMutexes[socket] = std::make_shared<std::mutex>();
                    }
                    std::lock_guard<std::mutex> lock(clientSocketsMutex);
                    clientSockets.push_back(socket);

                    ClientServiceLink::Log("Client connected", 1);
                } else {
                    ClientServiceLink::Log("Handshake failed: " + error.message(), 2);
                }
                AcceptConnections();
            });
        } else {
            ClientServiceLink::Log("Accept failed: " + error.message(), 2);

            AcceptConnections();
        }
    });
}

void ClientHandler::RecieveData() {
    using lowest_layer_type = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::lowest_layer_type;
    std::unordered_map<SOCKET, bool> readingSockets;
    std::unique_lock<std::mutex> lock(clientSocketsMutex);
    lock.unlock();

    while (!shutdown) {
        lock.lock();
        for (auto it = clientSockets.begin(); it != clientSockets.end(); ) {
            auto& socket = *it;

            std::unique_lock<std::mutex> socketLock(GetSocketMutex(socket));

            if (!socket || !socket->lowest_layer().is_open()) {
                readingSockets.erase(socket);
                socketLock.unlock();
                lock.unlock();
                Disconnect(socket);
                lock.lock();
                break;
            }

            auto& isReading = readingSockets[socket];
            socketLock.unlock();
            if (isReading) {
                ++it;
                continue;
            }

            isReading = true;
            auto buffer = std::make_shared<std::vector<char>>(1024);
            socketLock.lock();
            socket->async_read_some(boost::asio::buffer(*buffer),
              [buffer, &isReading, socket](const boost::system::error_code& error, std::size_t bytes_transferred) mutable {
                isReading = false;

                if (!socket) return;

                if (error) {
                    Disconnect(socket, error.message());
                    return;
                }

                std::string data(buffer->data(), bytes_transferred);
                long uid = 0;
                bool verified = false;
                {
                    std::lock_guard<std::mutex> unvSockLock(unverifiedSocketsMutex);
                    if (unverifiedSockets.find(socket) == unverifiedSockets.end()) {
                        uid = 1;
                    }
                }
                if (uid == 1) {
                    uid = GetUIDBySocket(socket);
                    verified = true;
                } else {
                    std::unique_lock<std::mutex> unvSockLock(unverifiedSocketsIDsMutex);
                    auto it = std::find_if(unverifiedSocketsIDs.begin(), unverifiedSocketsIDs.end(),
                                    [&socket](const auto& pair) { return pair.second == socket; });
                    if (it != unverifiedSocketsIDs.end()) {
                        unvSockLock.unlock();
                        uid = it->first;
                    } else {
                        unvSockLock.unlock();
                        Disconnect(socket, "Critical server error: Could not find client socket");
                        ClientServiceLink::Log("Critical server error: Could not find client socket", 4);
                        return;
                    }
                }
                data = TypeUtils::stickParams(verified, uid, data);

                std::lock_guard<std::mutex> lock(recieveBufferMutex);
                recieveBuffer.push(data);
            });

            ++it;
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void ClientHandler::ProcessData() {
    while (!shutdown) {
        while (true) {
            std::string data;
            {
                std::lock_guard<std::mutex> guard(recieveBufferMutex);
                if (recieveBuffer.empty()) {
                    break;
                }
                data = recieveBuffer.front();
                recieveBuffer.pop();
            }

            boost::asio::post(threadPool, [data]() {
                ProcessDataContent(data);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ClientHandler::SendDataFromBuffer() {
    while (!shutdown) {
        while (true) {
            std::unique_lock<std::mutex> sendLock(sendBufferMutex);
            if (sendBuffer.empty()) {
                sendLock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            while (!sendBuffer.empty()) {
                TypeUtils::Message msg = sendBuffer.front();
                std::string data = msg.content;
                auto socket = msg.socket;
                sendBuffer.pop();
                sendLock.unlock();

                std::lock_guard<std::mutex> lock(GetSocketMutex(socket));
                if (socket && socket->lowest_layer().is_open()) {
                    #ifdef DEBUG
                        std::cout << "Sent data: " << data << std::endl;
                    #endif
                    boost::asio::async_write(*socket, boost::asio::buffer(data), [](const boost::system::error_code& error, std::size_t) {
                        if (error) {
                            ClientServiceLink::Log("Send failed: " + error.message(), 3);
                        }
                    });
                }

                sendLock.lock();
            }
        }
    }
}

void ClientHandler::Disconnect(SOCKET socket, const std::string& reason) {
    if (!socket) return;

    {
        std::lock_guard<std::mutex> lock(disconnectMutex);
        if (std::find(disconnectingSockets.begin(), disconnectingSockets.end(), socket) != disconnectingSockets.end()) {
            return;
        }
        disconnectingSockets.push_back(socket);
    }
    
    if (!socket) return;
    
    {
        std::lock_guard<std::mutex> lock(sendBufferMutex);
        std::lock_guard<std::mutex> lock2(GetSocketMutex(socket));
        std::queue<TypeUtils::Message> tempQueue;

        while (!sendBuffer.empty()) {
            TypeUtils::Message msg = sendBuffer.front();
            sendBuffer.pop();

            if (msg.socket == socket) {
                std::lock_guard<std::mutex> socketLock(GetSocketMutex(socket));
                if (socket && socket->lowest_layer().is_open()) {
                    boost::asio::write(*socket, boost::asio::buffer(msg.content));
                }
            } else {
                tempQueue.push(msg);
            }
        }

        std::swap(sendBuffer, tempQueue);
    }

    {
        std::lock_guard<std::mutex> socketLock(GetSocketMutex(socket));
        if (socket && socket->lowest_layer().is_open()) {
            boost::asio::async_write(*socket, boost::asio::buffer(TypeUtils::stickParams("DISCONNECT", reason)),
                [socket](const boost::system::error_code&, std::size_t) {});

            boost::system::error_code ec;
            socket->lowest_layer().cancel(ec);

            socket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket->lowest_layer().close(ec);
        }
    }

    {
        long uid = 0;
        {
            std::lock_guard<std::mutex> lock(clientMapsMutex);
            auto it = socketToUIDMap.find(socket);
            if (it != socketToUIDMap.end()) {
                uid = it->second;
            }
        }

        std::lock_guard<std::mutex> lock(loggedInUsersMutex);
        loggedInUsers.erase(uid);
    }

    {
        std::lock_guard<std::mutex> lock(unverifiedSocketsMutex);
        unverifiedSockets.erase(socket);
    }

    {
        std::lock_guard<std::mutex> lock(unverifiedSocketsIDsMutex);
        auto it = std::find_if(unverifiedSocketsIDs.begin(), unverifiedSocketsIDs.end(),
                               [&socket](const auto& pair) { return pair.second == socket; });
        if (it != unverifiedSocketsIDs.end()) {
            unverifiedSocketsIDs.erase(it);
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(clientMapsMutex);
        auto uidIt = std::find_if(uidToSocketMap.begin(), uidToSocketMap.end(),
                                  [&socket](const auto& pair) { return pair.second == socket; });
        if (uidIt != uidToSocketMap.end()) {
            socketToUIDMap.erase(socket);
            uidToSocketMap.erase(uidIt);
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientSocketsMutex);
        auto it = std::find(clientSockets.begin(), clientSockets.end(), socket);
        if (it != clientSockets.end()) {
            clientSockets.erase(it);
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientSocketsMutexesMutex);
        auto it = clientSocketsMutexes.find(socket);
        if (it != clientSocketsMutexes.end()) {
            { std::lock_guard<std::mutex> lock2(*it->second); }
            clientSocketsMutexes.erase(socket);
        }
    }

    {
        std::lock_guard<std::mutex> lock(disconnectMutex);
        auto it = std::find(disconnectingSockets.begin(), disconnectingSockets.end(), socket);
        if (it != disconnectingSockets.end()) {
            disconnectingSockets.erase(it);
        }
    }

    ClientServiceLink::Log("Client disconnected: " + reason, 1);
}

std::mutex& ClientHandler::GetSocketMutex(SOCKET socket) {
    std::lock_guard<std::mutex> lock(clientSocketsMutexesMutex);
    return *clientSocketsMutexes[socket];
}

void ClientHandler::AddClient(long uid, SOCKET socket) {
    std::lock_guard<std::mutex> lock(clientMapsMutex);
    uidToSocketMap[uid] = socket;
    socketToUIDMap[socket] = uid;
}

void ClientHandler::RemoveClient(long uid) {
    std::lock_guard<std::mutex> lock(clientMapsMutex);
    auto socket = uidToSocketMap[uid];
    uidToSocketMap.erase(uid);
    socketToUIDMap.erase(socket);
}

SOCKET ClientHandler::GetSocketByUID(long uid) {
    std::lock_guard<std::mutex> lock(clientMapsMutex);
    auto it = uidToSocketMap.find(uid);
    return it->second;
}

long ClientHandler::GetUIDBySocket(SOCKET socket) {
    std::lock_guard<std::mutex> lock(clientMapsMutex);
    auto it = socketToUIDMap.find(socket);
    return it->second;
}

void ClientHandler::CheckUnverifiedSockets() {
    while (!shutdown) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(unverifiedSocketsMutex);

        for (auto it = unverifiedSockets.begin(); it != unverifiedSockets.end(); ) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 5) {
                auto socket = it->first;
                Disconnect(socket, "Authentication time expired");
            } else {
                ++it;
            }
        }
    }
}

void ClientHandler::ProcessDataContent(std::string data) {
    #ifdef DEBUG
        std::cerr << "Processing data: " << data << std::endl;
    #endif

    bool verified = TypeUtils::getFirstParam(data) == "1";
    long uid = stol(TypeUtils::getFirstParam(data));
    std::string action = TypeUtils::getFirstParam(data);

    if (!verified) {
        SOCKET socket;
        {
            std::lock_guard<std::mutex> lock(unverifiedSocketsIDsMutex);
            socket = unverifiedSocketsIDs[uid];
        }
        if (!TypeUtils::isValidString(action)) {
            Disconnect(socket, "Invalid data");
            return;
        }

        if (action == "LOGIN") {
            std::string strUid = TypeUtils::getFirstParam(data);
            unsigned long _uid;
            std::string token = TypeUtils::getFirstParam(data);
            if (!TypeUtils::tryPassULong(strUid, _uid) || !TypeUtils::isValidString(token)) {
                Disconnect(socket, "Invalid data");
                return;
            }

            short err;

            {
                std::lock_guard<std::mutex> lock(unverifiedSocketsIDsMutex);
                if (loggedInUsers.find(_uid) == loggedInUsers.end()) {
                    Disconnect(socket, "Not logged in");
                    return;
                }
                if (loggedInUsers[_uid] != token) {
                    Disconnect(socket, "Invalid token");
                    return;
                }
            }

            AddClient(_uid, socket);

            return;
        }

        Disconnect(socket, "Invalid data");
        return;
    }

    SOCKET socket = GetSocketByUID(uid);

    if (action.empty()) {
        Disconnect(socket, "Invalid data");
        return;
    }

    Disconnect(socket, "Invalid data");
}
