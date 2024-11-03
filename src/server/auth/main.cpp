// #include "shared/ClientServiceLink.hpp"
// #include "ClientHandler.hpp"
// #include "messageHandler.hpp"
// #include "Settings.hpp"
// #include "defines.hpp"

// #include <thread>
// #include <chrono>
// #include <iostream>


// int main() {
// std::mutex testMutex;
// std::cerr << "function test (should be 0): " << TypeUtils::isLocked(testMutex) << std::endl;

// {
//     std::lock_guard<std::mutex> lock(testMutex);
//     std::cerr << "function test (should be 1): " << TypeUtils::isLocked(testMutex) << std::endl;
// }

// std::cerr << "function test (should be 0): " << TypeUtils::isLocked(testMutex) << std::endl;

//     ClientServiceLink::SetMessageHandler(handleMessageContent);
//     std::thread connectionThread(&ClientServiceLink::StartClient, DIR + "auth");

//     while (!allSettingsReceived()) {
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     }
    
//     std::cout << "All settings received." << std::endl;

//     ClientHandler::Init(settings.port);
//     ClientHandler::Start();



//     // ----------------- Add here some other code ----------------- //



//     connectionThread.join();

//     std::cerr << "Exiting main" << std::endl;
//     return 0;
// }

#include <iostream>
#include <mutex>

namespace TypeUtils123123 {
    bool isLocked(std::mutex& m) {
        std::cerr << "Inside isLocked" << std::endl;
        std::unique_lock<std::mutex> lock(m, std::try_to_lock);
        if (lock.owns_lock()) {
            return false; // Mutex was not locked; we now own it
        } else {
            return true;  // Mutex is already locked
        }
    }
}

int main() {
    std::mutex testMutex;
    std::cerr << "function test (should be 0): " << TypeUtils123123::isLocked(testMutex) << std::endl;

    {
        std::lock_guard<std::mutex> lock(testMutex);
        std::cerr << "function test (should be 1): " << TypeUtils123123::isLocked(testMutex) << std::endl;
    }

    std::cerr << "function test (should be 0): " << TypeUtils123123::isLocked(testMutex) << std::endl;

    return 0;
}
