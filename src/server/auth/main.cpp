#include "defines.hpp"
#include "Settings.hpp"
#include "ClientHandler.hpp"
#include "messageHandler.hpp"
#include "shared/ClientServiceLink.hpp"

#include <thread>
#include <chrono>

#ifdef DEBUG
    #include <iostream>
#endif


int main() {
    ClientServiceLink::SetMessageHandler(handleMessageContent);
    std::thread connectionThread(&ClientServiceLink::StartClient, DIR + "auth");

    while (!allSettingsReceived()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    ClientServiceLink::Log("All settings received", 1);

    ClientHandler::Init(settings.port);
    ClientHandler::Start();



    // ----------------- Add here some other code ----------------- //



    connectionThread.join();

    #ifdef DEBUG
        std::cerr << "Exiting main" << std::endl;
    #endif
    return 0;
}
