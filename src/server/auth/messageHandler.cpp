#include "messageHandler.hpp"

#include "Settings.hpp"
#include "ClientHandler.hpp"
#include "common/TypeUtils.hpp"
#include "shared/ClientServiceLink.hpp"


void handleMessageContent(std::string message) {
    std::string action = TypeUtils::getFirstParam(message);
    std::string data = TypeUtils::getFirstParam(message);

    if (action == "SETTING_SET_dbConnString") {
        settings.dbConnString = data;
        allSettingsReceivedArray[0] = true;

    } else if (action == "SETTING_SET_port") {
        settings.port = std::stoi(data);
        allSettingsReceivedArray[1] = true;

    } else if (action == "SETTING_SET_emailVerificationsAttempts") {
        settings.emailVerificationsAttempts = std::stoi(data);
        allSettingsReceivedArray[2] = true;

    } else if (action == "SETTING_SET_loginAttempts") {
        settings.loginAttempts = std::stoi(data);
        allSettingsReceivedArray[3] = true;

    } else if (action == "SETTING_SET_loginTime") {
        settings.loginTime = std::stoi(data);
        allSettingsReceivedArray[4] = true;

    } else if (action == "SETTING_SET_emailVerificationTime") {
        settings.emailVerificationTime = std::stoi(data);
        allSettingsReceivedArray[5] = true;

    } else if (action == "SHUTTING_DOWN") {
        ClientHandler::InitiateShutdown();

    } else if (action == "SHUTDOWN") {
        ClientHandler::Shutdown();
        ClientServiceLink::running = false;
        
    } else {
        ClientServiceLink::Log("Unknown action sent from control: " + action, 3);
    }
}
