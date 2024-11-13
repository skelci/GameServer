#include "ClientHandler.hpp"

#include "Auth.hpp"
#include "defines.hpp"
#include "Settings.hpp"
#include "shared/ClientServiceLink.hpp"

#include <boost/asio/post.hpp>

#include <thread>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

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

std::unordered_map<unsigned short, UserPreregister> ClientHandler::userPreregister;
unsigned short ClientHandler::userPreregisterCounter;
std::mutex ClientHandler::userPreregisterMutex;

std::unordered_map<long, UserLogin> ClientHandler::userLogin;
std::mutex ClientHandler::userLoginMutex;

std::atomic<bool> ClientHandler::running(true);
std::atomic<bool> ClientHandler::shutdown(false);

std::mutex ClientHandler::disconnectMutex;
std::vector<SOCKET> ClientHandler::disconnectingSockets;

boost::asio::thread_pool ClientHandler::threadPool(std::thread::hardware_concurrency());

unsigned short ClientHandler::emailVerificationsAttempts;
unsigned short ClientHandler::loginAttempts;
unsigned short ClientHandler::loginTime;
unsigned short ClientHandler::emailVerificationTime;


std::mutex& ClientHandler::GetSocketMutex(SOCKET socket) {
    std::lock_guard<std::mutex> lock(clientSocketsMutexesMutex);
    return *clientSocketsMutexes[socket];
}

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

    emailVerificationsAttempts = settings.emailVerificationsAttempts;
    loginAttempts = settings.loginAttempts;
    loginTime = settings.loginTime;
    emailVerificationTime = settings.emailVerificationTime;

    Auth::Init();
}

void ClientHandler::Start() {
    std::thread(&ClientHandler::AcceptConnections).detach();
    std::thread(&ClientHandler::RecieveData).detach();
    std::thread(&ClientHandler::ProcessData).detach();
    std::thread(&ClientHandler::SendDataFromBuffer).detach();

    std::cerr << "Running io_context..." << std::endl << std::flush;
    for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
        std::thread([]() {
            while (!shutdown) {
                io_context.run();
                io_context.restart();
            }
        }).detach();
    }

    std::thread(&ClientHandler::CheckUnverifiedSockets).detach();
    std::thread(&ClientHandler::CheckUserPreregister).detach();
    std::thread(&ClientHandler::CheckUserLogin).detach();
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
    std::cout << "Server has been shut down." << std::endl;
    ClientServiceLink::SendData("LOG", 2, "Server has been shut down.");
}

void ClientHandler::AcceptConnections() {
    if (!running) return;

    #ifdef DEBUG
        std::cout << std::flush;
        std::cout << "Waiting to accept connections..." << std::endl;
    #endif
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

                    #ifdef DEBUG
                        std::cout << "Accepted connection" << std::endl;
                    #endif
                } else {
                    std::cerr << "Handshake failed: " << error.message() << std::endl;
                    ClientServiceLink::SendData("LOG", 4, "Handshake failed: " + error.message());
                }
                AcceptConnections();
            });
        } else {
            std::cerr << "Accept failed: " << error.message() << std::endl;
            ClientServiceLink::SendData("LOG", 4, "Accept failed: " + error.message());

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
                        ClientServiceLink::SendData("LOG", 5, "Critical server error: Could not find client socket");
                        std::cerr << "Critical server error: Could not find client socket" << std::endl;
                        return;
                    }
                }
                data = TypeUtils::stickParams(verified, uid, data);

                std::lock_guard<std::mutex> lock(recieveBufferMutex);
                recieveBuffer.push(data);
                #ifdef DEBUG
                    std::cout << "Received data from client: " << std::string(buffer->data(), bytes_transferred) << "\n";
                #endif
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
                            std::cerr << "Error sending data: " << error.message() << std::endl;
                            ClientServiceLink::SendData("LOG", 4, "Error sending data: " + error.message());
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
        std::lock_guard<std::mutex> lock(userPreregisterMutex);
        auto it = std::find_if(userPreregister.begin(), userPreregister.end(),
                               [&socket](const auto& pair) { return GetSocketByUID(-(pair.first)) == socket; });
        if (it != userPreregister.end()) {
            userPreregister.erase(it);
        }
    }

    {
        std::lock_guard<std::mutex> lock(userLoginMutex);
        auto it = std::find_if(userLogin.begin(), userLogin.end(),
                               [&socket](const auto& pair) { return GetSocketByUID(pair.first) == socket; });
        if (it != userLogin.end()) {
            userLogin.erase(it);
        }
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

    #ifdef DEBUG
        std::cout << std::flush;
        std::cout << "\n";
        std::cout << "ClientSockets: " << clientSockets.size() << "\n";
        std::cout << "ClientSocketsMutexes: " << clientSocketsMutexes.size() << "\n";
        std::cout << "RecieveBuffer: " << recieveBuffer.size()<< "\n";
        std::cout << "SendBuffer: " << sendBuffer.size() << "\n";
        std::cout << "UIDToSocketMap: " << uidToSocketMap.size() << "\n";
        std::cout << "SocketToUIDMap: " << socketToUIDMap.size() << "\n";
        std::cout << "UnverifiedSockets: " << unverifiedSockets.size() << "\n";
        std::cout << "UnverifiedSocketsIDs: " << unverifiedSocketsIDs.size() << "\n";
        std::cout << "UserPreregister: " << userPreregister.size() << "\n";
        std::cout << "UserLogin: " << userLogin.size() << "\n";
        std::cout << "DisconnectingSockets: " << disconnectingSockets.size() << "\n";
        std::cout << "\n";
        std::cout << std::flush;
    #endif

    std::cout << "Disconnected client: " << reason << std::endl;
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
            if (std::chrono::duration_cast<std::chrono::minutes>(now - it->second).count() > loginTime) {
                auto socket = it->first;
                Disconnect(socket, "Authentication time expired");
            } else {
                ++it;
            }
        }
    }
}

void ClientHandler::CheckUserPreregister() {
    while (!shutdown) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(userPreregisterMutex);

        for (auto it = userPreregister.begin(); it != userPreregister.end(); ) {
            if (std::chrono::duration_cast<std::chrono::minutes>(now - it->second.time).count() > emailVerificationTime) {
                auto socket = GetSocketByUID(it->first);
                Disconnect(socket, "Email verification time expired");
            } else {
                ++it;
            }
        }
    }
}

void ClientHandler::CheckUserLogin() {
    while (!shutdown) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(userLoginMutex);

        for (auto it = userLogin.begin(); it != userLogin.end(); ) {
            if (std::chrono::duration_cast<std::chrono::minutes>(now - it->second.time).count() > loginTime) {
                auto socket = GetSocketByUID(it->first);
                Disconnect(socket, "Login time expired");
            } else {
                ++it;
            }
        }
    }
}

short ClientHandler::SendEmail(std::string email, std::string subject, std::string content, SOCKET socket) {
    //TODO Implement email sending logic

    SendData(socket, subject, content);

    return 1;
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

        if (action == "LOGIN_PREPARE") {
            std::string username = TypeUtils::getFirstParam(data);
            std::string password = TypeUtils::getFirstParam(data);
            if (!TypeUtils::isValidString(username) || !TypeUtils::isValidString(password)) {
                Disconnect(socket, "Invalid data");
                return;
            }

            short err;

            if (uid = Auth::GetUID(username), uid == 0) {
                SendData(socket, "LOGIN", "WRONG_USERNAME");
                return;
            } else if (uid < 0) {
                Disconnect(socket, "Critical server error: Error during login");
                std::cerr << "Error getting UID: " << uid << std::endl;
                ClientServiceLink::SendData("LOG", 5, "Error getting UID: " + std::to_string(uid));
                return;
            }

            if (err = Auth::VerifyPassword(uid, password), err == 0) {
                SendData(socket, "LOGIN", "WRONG_PASSWORD");
                return;
            } else if (err != 1) {
                Disconnect(socket, "Critical server error: Error during login");
                std::cerr << "Error verifying password: " << err << std::endl;
                ClientServiceLink::SendData("LOG", 5, "Error verifying password: " + std::to_string(err));
                return;
            }

            std::string email;
            if (err = Auth::GetEmail(uid, email), err != 1) {
                Disconnect(socket, "Critical server error: Error during login");
                std::cerr << "Error getting email: " << err << std::endl;
                ClientServiceLink::SendData("LOG", 5, "Error getting email: " + std::to_string(err));
                return;
            }

            {
                std::lock_guard<std::mutex> lock(unverifiedSocketsIDsMutex);
                unverifiedSocketsIDs.erase(uid);
            }
            {
                std::lock_guard<std::mutex> lock(unverifiedSocketsMutex);
                unverifiedSockets.erase(socket);
            }
            unsigned emailCode = TypeUtils::randint(0, 1'000'000-1);
            auto time = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(userLoginMutex);
                userLogin[uid] = {uid, time, email, emailCode, 0};
            }

            SendData(socket, "LOGIN", "PREPARE_SUCCESS", emailVerificationTime);
            {
                if (SendEmail(email, "Email verification", "Your verification code is: " + std::to_string(emailCode), socket) != 1) {
                    Disconnect(socket, "Critical server error: Error during login");
                    return;
                }
            }

            AddClient(uid, socket);

            return;
        
        } else if (action == "REGISTER_PREPARE") {
            std::string username = TypeUtils::getFirstParam(data);
            std::string password = TypeUtils::getFirstParam(data);
            std::string email = TypeUtils::getFirstParam(data);
            if (!TypeUtils::isValidString(username) || !TypeUtils::isValidString(password) || !TypeUtils::isValidString(email)) {
                Disconnect(socket, "Invalid data");
                return;
            }

            short err;
            if (err = Auth::CheckUsername(username), err == 1) {
                SendData(socket, "REGISTER", "USERNAME_EXISTS");
                return;
            } else if (err != 0) {
                ClientServiceLink::SendData("LOG", 5, "Error checking username: " + std::to_string(err));
                Disconnect(socket, "Critical server error during registration");
                std::cerr << "Error checking username: " << err << std::endl;
                return;
            }

            else if (err = Auth::CheckEmail(email), err == 1) {
                SendData(socket, "REGISTER", "EMAIL_EXISTS");
                return;
            } else if (err != 0) {
                ClientServiceLink::SendData("LOG", 5, "Error checking email: " + std::to_string(err));
                Disconnect(socket, "Critical server error during registration");
                std::cerr << "Error checking email: " << err << std::endl;
                return;
            }
            else if (err = Auth::CheckPassword(password), err == false) {
                Disconnect(socket, "Client error: Outdated/cheated client");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(unverifiedSocketsIDsMutex);
                unverifiedSocketsIDs.erase(uid);
            }
            {
                std::lock_guard<std::mutex> lock(unverifiedSocketsMutex);
                unverifiedSockets.erase(socket);
            }
            unsigned emailCode = TypeUtils::randint(0, 1'000'000-1);
            auto time = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(userPreregisterMutex);
                userPreregister[++userPreregisterCounter] = {username, password, email, emailCode, time, 0};
                AddClient(-(userPreregisterCounter), socket);
            }

            SendData(socket, "REGISTER", "PREPARE_SUCCESS", emailVerificationTime);
            if (SendEmail(email, "Email verification", "Your verification code is: " + std::to_string(emailCode), socket) != 1) {
                Disconnect(socket, "Critical server error during registration");
                return;
            }
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

    if (action == "REGISTER") {
        std::string strEmailCode = TypeUtils::getFirstParam(data);
        if (!TypeUtils::isValidString(strEmailCode)) {
            Disconnect(socket, "Invalid data");
            return;
        }
        unsigned emailCode;
        if (!TypeUtils::tryPassUInt(strEmailCode, emailCode)) {
            Disconnect(socket, "Invalid data");
            return;
        }

        UserPreregister user;
        {
            std::unique_lock<std::mutex> lock(userPreregisterMutex);
            auto it = userPreregister.find(-uid);
            if (it == userPreregister.end()) {
                lock.unlock();
                Disconnect(socket, "Invalid data");
                return;
            }
            user = it->second;
        }

        if (user.emailCode != emailCode) {
            SendData(socket, "REGISTER", "EMAIL_CODE_INCORRECT");
            {
                std::lock_guard<std::mutex> lock(userPreregisterMutex);
                userPreregister[uid] = user;
            }
            return;
        }
        if (user.attempts++ >= emailVerificationsAttempts) {
            Disconnect(socket, "Email verification attempts exceeded");
            return;
        }

        short err;
        if (err = Auth::RegisterUser(user.username, user.password, user.email), err == 1) {
            RemoveClient(uid);
            {
                std::lock_guard<std::mutex> lock(userPreregisterMutex);
                userPreregister.erase(uid);
            }
        } else if (err == 0) {
            SendData(socket, "REGISTER", "ERROR");
            return;
        } else {
            Disconnect(socket, "Critical server error during registration");
            std::cerr << "Error registering user: " << err << std::endl;
            ClientServiceLink::SendData("LOG", 5, "Error registering user: " + std::to_string(err));
            return;
        }

        long oldUID = uid;
        if (uid = Auth::GetUID(user.username), uid < 1) {
            Disconnect(socket, "Critical server error during registration");
            std::cerr << "Error getting UID: " << uid << std::endl;
            ClientServiceLink::SendData("LOG", 5, "Error getting UID: " + std::to_string(uid));
            return;
        }
        AddClient(uid, socket);

        {
            std::lock_guard<std::mutex> lock(userPreregisterMutex);
            userPreregister.erase(-oldUID);
        }

        SendData(socket, "RELOGIN_TOKEN", Auth::CreateReloginToken(uid));

        return;
    
    } else if (action == "LOGIN") {
        std::string strEmailCode = TypeUtils::getFirstParam(data);
        if (!TypeUtils::isValidString(strEmailCode)) {
            Disconnect(socket, "Invalid data");
            return;
        }
        unsigned emailCode;
        if (!TypeUtils::tryPassUInt(strEmailCode, emailCode)) {
            Disconnect(socket, "Invalid data");
            return;
        }

        UserLogin user;
        {
            std::unique_lock<std::mutex> lock(userLoginMutex);
            auto it = userLogin.find(uid);
            if (it == userLogin.end()) {
                lock.unlock();
                Disconnect(socket, "Invalid data");
                return;
            }
            user = it->second;
        }

        if (user.emailCode != emailCode) {
            SendData(socket, "LOGIN", "EMAIL_CODE_INCORRECT");
            {
                std::lock_guard<std::mutex> lock(userLoginMutex);
                userLogin[uid] = user;
            }
            return;
        }
        if (user.attempts++ >= loginAttempts) {
            Disconnect(socket, "Login attempts exceeded");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(userLoginMutex);
            userLogin.erase(uid);
        }

        SendData(socket, "RELOGIN_TOKEN", Auth::CreateReloginToken(uid));

        return;
    }

    Disconnect(socket, "Invalid data");
}
