#pragma once

#include <pqxx/pqxx>

#include <mutex>
#include <string>
#include <atomic>
#include <unordered_map>


class Auth {
public:
    static void Init();

    static short RegisterUser(const std::string& username, const std::string& password, const std::string& email);

    static short ChangePassword(int uid, const std::string& oldPassword, const std::string& newPassword);

    static short VerifyPassword(int uid, const std::string& password);

    static short CheckUsername(const std::string& username);
    static short CheckEmail(const std::string& email);

    static std::string CreateReloginToken(int uid);
    static short VerifyReloginToken(int uid, const std::string& token);

    static int GetUID(const std::string& username);
    static short GetEmail(int uid, std::string& email);

private:
    static std::atomic<std::shared_ptr<pqxx::connection>> c;

    static std::mutex mainMutex;
};

// ----------------------------------------------------------------------------------------

/* Error codes:
    * 1: No error / User exists / Email exists
    * 0: Wrong password / Username does not exist / Email does not exist
    * -1: User not found
    * -9: Catch-all error
    * -10: Database connection is not open
*/
