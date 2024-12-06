#include "messageHandler.hpp"

#include "Settings.hpp"
#include "ClientHandler.hpp"
#include "common/TypeUtils.hpp"
#include "shared/ClientServiceLink.hpp"

#include <string>


void handleMessageContent(const std::string& message) {
    std::string msg = message;
    std::string action = TypeUtils::getFirstParam(msg);

    if (action == "LOGIN") {
        unsigned long uid = std::stoul(TypeUtils::getFirstParam(msg));
        std::string token = TypeUtils::getFirstParam(msg);
        std::lock_guard<std::mutex> lock(ClientHandler::loggedInUsersMutex);
        ClientHandler::loggedInUsers[uid] = token;

    } else if (action == "SETTING_SET_port") {
        settings.port = std::stoi(msg);
        allSettingsReceivedArray[0] = true;

    } else if (action == "SHUTTING_DOWN") {
        ClientHandler::InitiateShutdown();

    } else if (action == "SHUTDOWN") {
        ClientHandler::Shutdown();
        ClientServiceLink::running = false;
    
    } else {
        ClientServiceLink::Log("Unknown action: " + action, 4);
    }
}
