#include "Auth.hpp"

#include "Settings.hpp"
#include "common/TypeUtils.hpp"
#include "shared/ClientServiceLink.hpp"

#include <stdexcept>

std::atomic<std::shared_ptr<pqxx::connection>> Auth::c;
std::mutex Auth::mainMutex;


void Auth::Init() {
    std::lock_guard<std::mutex> lock(mainMutex);

    c = std::make_unique<pqxx::connection>(settings.dbConnString);
    if (!c.load()->is_open()) {
        throw std::runtime_error("Failed to open database connection");
    }

    pqxx::work txn(*c.load());
    txn.exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "uid SERIAL PRIMARY KEY, "
        "username VARCHAR(255) NOT NULL, "
        "email VARCHAR(255) NOT NULL, "
        "createdAt DOUBLE PRECISION NOT NULL, "
        "passwordHash VARCHAR(255) NOT NULL, "
        "passwordSalt VARCHAR(255) NOT NULL, "
        "reloginTokenHash VARCHAR(255), "
        "reloginTokenSalt VARCHAR(255)"
        ");"
    );
    txn.commit();
}

short Auth::RegisterUser(const std::string& username, const std::string& password, const std::string& email) {
    std::lock_guard<std::mutex> lock(mainMutex);

    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        std::string salt;
        do {
            salt = TypeUtils::generateSalt();
        } while (salt == "");
        std::string passwordHash = TypeUtils::hashString(password, salt);
        double createdAt = TypeUtils::getCurrentTimestamp();

        pqxx::work txn(*conn);
        txn.exec_params(
            "INSERT INTO users (username, email, createdAt, passwordHash, passwordSalt) VALUES ($1, $2, $3, $4, $5)",
            username, email, createdAt, passwordHash, salt
        );
        txn.commit();

        return 1;
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error registering user: " + std::string(e.what()), 4);
        return -9;
    }
}

short Auth::ChangePassword(int uid, const std::string& oldPassword, const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        short verifyResult = VerifyPassword(uid, oldPassword);
        if (verifyResult != 1) {
            return verifyResult;
        }

        std::string newSalt;
        do {
            newSalt = TypeUtils::generateSalt();
        } while (newSalt == "");
        std::string newPasswordHash = TypeUtils::hashString(newPassword, newSalt);

        pqxx::work txn(*conn);
        txn.exec_params(
            "UPDATE users SET passwordHash = $1, passwordSalt = $2 WHERE uid = $3",
            newPasswordHash, newSalt, uid
        );
        txn.commit();

        return 1;
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error changing password: " + std::string(e.what()), 4);
        return -9;
    }
}

short Auth::VerifyPassword(int uid, const std::string& password) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec_params(
            "SELECT passwordHash, passwordSalt FROM users WHERE uid = $1",
            uid
        );

        if (result.empty()) { // User not found
            return -1;
        }

        std::string storedHash = result[0]["passwordHash"].as<std::string>();
        std::string storedSalt = result[0]["passwordSalt"].as<std::string>();

        std::string computedHash = TypeUtils::hashString(password, storedSalt);

        if (storedHash == computedHash) return 1;
        else return 0;
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error verifying password: " + std::string(e.what()), 4);
        return -9;
    }
}

short Auth::CheckUsername(const std::string& username) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec_params(
            "SELECT COUNT(*) FROM users WHERE username = $1",
            username
        );

        if (result[0][0].as<int>() > 0) {
            return 1; // Username exists
        } else {
            return 0; // Username does not exist
        }
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error checking username: " + std::string(e.what()), 4);
        return -9;
    }
}

short Auth::CheckEmail(const std::string& email) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec_params(
            "SELECT COUNT(*) FROM users WHERE email = $1",
            email
        );

        if (result[0][0].as<int>() > 0) {
            return 1; // Email exists
        } else {
            return 0; // Email does not exist
        }
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error checking email: " + std::string(e.what()), 4);
        return -9;
    }
}

std::string Auth::CreateReloginToken(int uid) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return "";
        }

        std::string reloginToken;
        std::string reloginTokenSalt;
        do {
            reloginToken = TypeUtils::generateSalt();
        } while (reloginToken == "");
        do {
            reloginTokenSalt = TypeUtils::generateSalt();
        } while (reloginTokenSalt == "");

        std::string reloginTokenHash = TypeUtils::hashString(reloginToken, reloginTokenSalt);

        pqxx::work txn(*conn);
        txn.exec_params(
            "UPDATE users SET reloginTokenHash = $1, reloginTokenSalt = $2 WHERE uid = $3",
            reloginTokenHash, reloginTokenSalt, uid
        );
        txn.commit();

        return reloginToken;
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error creating relogin token: " + std::string(e.what()), 4);
        return "";
    }
}

short Auth::VerifyReloginToken(int uid, const std::string& token) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec_params(
            "SELECT reloginTokenHash, reloginTokenSalt FROM users WHERE uid = $1",
            uid
        );

        if (result.empty()) { // User not found
            return -1;
        }

        std::string storedHash = result[0]["reloginTokenHash"].as<std::string>();
        std::string storedSalt = result[0]["reloginTokenSalt"].as<std::string>();

        std::string computedHash = TypeUtils::hashString(token, storedSalt);

        if (storedHash == computedHash) return 1;
        else return 0;
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error verifying relogin token: " + std::string(e.what()), 4);
        return -9;
    }
}

int Auth::GetUID(const std::string& username) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec_params(
            "SELECT uid FROM users WHERE username = $1",
            username
        );

        if (result.empty()) { // User not found
            return 0;
        }

        return result[0]["uid"].as<int>();
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error getting UID: " + std::string(e.what()), 4);
        return -9;
    }
}

short Auth::GetEmail(int uid, std::string& email) {
    std::lock_guard<std::mutex> lock(mainMutex);
    try {
        auto conn = c.load();
        if (!conn) {
            ClientServiceLink::Log("Database connection is not open", 4);
            return -10;
        }

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec_params(
            "SELECT email FROM users WHERE uid = $1",
            uid
        );

        if (result.empty()) { // User not found
            return -1;
        }

        email = result[0]["email"].as<std::string>();
        return 1;
    } catch (const std::exception& e) {
        ClientServiceLink::Log("Error getting email: " + std::string(e.what()), 4);
        return -9;
    }
}
