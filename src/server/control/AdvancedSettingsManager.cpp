#include "AdvancedSettingsManager.hpp"

#include "Log.hpp"
#include "AdminConsole.hpp"

#include <jsoncpp/json/json.h>

#include <fstream>

std::string AdvancedSettingsManager::file;
Config AdvancedSettingsManager::config;
std::mutex AdvancedSettingsManager::mutex;
std::vector<std::variant<int*, std::string*>> AdvancedSettingsManager::settings;


void AdvancedSettingsManager::LoadSettings(const std::string& file_) {
    settings = {
        &config.controlServiceIp,
        &config.controlServicePort,

        &config.logLevel,
        &config.maxLogBufferSize,

        &config.dbname,
        &config.dbuser,
        &config.dbpassword,
        &config.dbhostaddr,
        &config.dbport,
        
        &config.commandPrompt,
        &config.commandWindowHeight,

        &config.authServicePort,
        &config.emailVerificationsAttempts,
        &config.loginAttempts,
        &config.loginTime,
        &config.emailVerificationTime,

        &config.worldServicePort
    };

    std::lock_guard<std::mutex> lock(mutex);
    file = file_;

    std::ifstream configFile(file, std::ifstream::binary);
    if (configFile) {
        Json::Value root;
        configFile >> root;

        if (root.isMember("controlServiceIp")) {
            config.controlServiceIp = root["controlServiceIp"].asString();
        }
        if (root.isMember("port")) {
            config.controlServicePort = root["controlServicePort"].asInt();
        }

        if (root.isMember("logLevel")) {
            config.logLevel = root["logLevel"].asInt();
        }
        if (root.isMember("maxLogBufferSize")) {
            config.maxLogBufferSize = root["maxLogBufferSize"].asInt();
        }

        if (root.isMember("dbname")) {
            config.dbname = root["dbname"].asString();
        }
        if (root.isMember("dbuser")) {
            config.dbuser = root["dbuser"].asString();
        }
        if (root.isMember("dbpassword")) {
            config.dbpassword = root["dbpassword"].asString();
        }
        if (root.isMember("dbhostaddr")) {
            config.dbhostaddr = root["dbhostaddr"].asString();
        }
        if (root.isMember("dbport")) {
            config.dbport = root["dbport"].asInt();
        }

        if (root.isMember("commandPrompt")) {
            config.commandPrompt = root["commandPrompt"].asString();
        }
        if (root.isMember("commandWindowHeight")) {
            config.commandWindowHeight = root["commandWindowHeight"].asInt();
        }

        if (root.isMember("authServicePort")) {
            config.authServicePort = root["authServicePort"].asInt();
        }
        if (root.isMember("emailVerificationsAttempts")) {
            config.emailVerificationsAttempts = root["emailVerificationsAttempts"].asInt();
        }
        if (root.isMember("loginAttempts")) {
            config.loginAttempts = root["loginAttempts"].asInt();
        }
        if (root.isMember("loginTime")) {
            config.loginTime = root["loginTime"].asInt();
        }
        if (root.isMember("emailVerificationTime")) {
            config.emailVerificationTime = root["emailVerificationTime"].asInt();
        }

        if (root.isMember("worldServicePort")) {
            config.worldServicePort = root["worldServicePort"].asInt();
        }
    }
}

void AdvancedSettingsManager::SaveSettings() {
    std::lock_guard<std::mutex> lock(mutex);

    Json::Value root;
    root["controlServiceIp"] = config.controlServiceIp;
    root["controlServicePort"] = config.controlServicePort;

    root["logLevel"] = config.logLevel;
    root["maxLogBufferSize"] = config.maxLogBufferSize;

    root["dbname"] = config.dbname;
    root["dbuser"] = config.dbuser;
    root["dbpassword"] = config.dbpassword;
    root["dbhostaddr"] = config.dbhostaddr;
    root["dbport"] = config.dbport;

    root["commandPrompt"] = config.commandPrompt;
    root["commandWindowHeight"] = config.commandWindowHeight;

    root["authServicePort"] = config.authServicePort;
    root["emailVerificationsAttempts"] = config.emailVerificationsAttempts;
    root["loginAttempts"] = config.loginAttempts;
    root["loginTime"] = config.loginTime;
    root["emailVerificationTime"] = config.emailVerificationTime;

    root["worldServicePort"] = config.worldServicePort;

    std::ofstream configFile(file, std::ofstream::binary);
    configFile << root;

    Log::Print("Settings saved to " + file, 1);
}

Config AdvancedSettingsManager::GetSettings() {
    std::lock_guard<std::mutex> lock(mutex);
    return config;
}

void AdvancedSettingsManager::SetSettings(
    std::optional<std::string> controlSericeIp,
    std::optional<int> port,

    std::optional<int> logLevel,
    std::optional<int> maxLogBufferSize,

    std::optional<std::string> dbname,
    std::optional<std::string> dbuser,
    std::optional<std::string> dbpassword,
    std::optional<std::string> dbhostaddr,
    std::optional<int> dbport,

    std::optional<std::string> commandPrompt,
    std::optional<int> commandWindowHeight,

    std::optional<int> authServicePort,
    std::optional<int> emailVerificationsAttempts,
    std::optional<int> loginAttempts,
    std::optional<int> loginTime,
    std::optional<int> emailVerificationTime,

    std::optional<int> worldServicePort
) {

    std::lock_guard<std::mutex> lock(mutex);

    if (controlSericeIp.has_value()) {
        config.controlServiceIp = controlSericeIp.value();
    }
    if (port.has_value()) {
        config.controlServicePort = port.value();
    }
    if (logLevel.has_value()) {
        config.logLevel = logLevel.value();
    }
    if (maxLogBufferSize.has_value()) {
        config.maxLogBufferSize = maxLogBufferSize.value();
    }
    if (dbname.has_value()) {
        config.dbname = dbname.value();
    }
    if (dbuser.has_value()) {
        config.dbuser = dbuser.value();
    }
    if (dbpassword.has_value()) {
        config.dbpassword = dbpassword.value();
    }
    if (dbhostaddr.has_value()) {
        config.dbhostaddr = dbhostaddr.value();
    }
    if (dbport.has_value()) {
        config.dbport = dbport.value();
    }
    if (commandPrompt.has_value()) {
        config.commandPrompt = commandPrompt.value();
    }
    if (commandWindowHeight.has_value()) {
        config.commandWindowHeight = commandWindowHeight.value();
    }
    if (authServicePort.has_value()) {
        config.authServicePort = authServicePort.value();
    }
    if (emailVerificationsAttempts.has_value()) {
        config.emailVerificationsAttempts = emailVerificationsAttempts.value();
    }
    if (loginAttempts.has_value()) {
        config.loginAttempts = loginAttempts.value();
    }
    if (loginTime.has_value()) {
        config.loginTime = loginTime.value();
    }
    if (emailVerificationTime.has_value()) {
        config.emailVerificationTime = emailVerificationTime.value();
    }
    if (worldServicePort.has_value()) {
        config.worldServicePort = worldServicePort.value();
    }
}

void AdvancedSettingsManager::SetSetting(size_t index, const std::variant<int, std::string>& value) {
    if (index >= settings.size()) {
        throw std::out_of_range("Index out of range");
    }

    std::visit([index](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            if (auto ptr = std::get_if<int*>(&settings[index])) {
                **ptr = arg;
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (auto ptr = std::get_if<std::string*>(&settings[index])) {
                **ptr = arg;
            }
        }
    }, value);
}

std::string AdvancedSettingsManager::GetSetting(size_t index) {
    if (index >= settings.size()) {
        throw std::out_of_range("Index out of range");
    }

    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            return std::to_string(*arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return *arg;
        } else if constexpr (std::is_same_v<T, int*>) {
            return std::to_string(*arg);
        } else if constexpr (std::is_same_v<T, std::string*>) {
            return *arg;
        } else {
            throw std::runtime_error(std::string("Unsupported type: ") + typeid(T).name());
        }
    }, settings[index]);
}
