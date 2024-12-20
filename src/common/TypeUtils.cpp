#include "TypeUtils.hpp"

#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#include <chrono>
#include <random>
#include <iostream>
#include <charconv>

namespace TypeUtils {

double getCurrentTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto now_duration = now.time_since_epoch();
    auto now_seconds = duration_cast<seconds>(now_duration).count();
    auto now_microseconds = duration_cast<microseconds>(now_duration).count() % 1000000;

    int days_since_epoch = now_seconds / 86400;
    double fractional_day = (now_seconds % 86400 * 1000000 + now_microseconds) / (86400.0 * 1000000);

    return days_since_epoch + fractional_day;
}

unsigned randint(unsigned min, unsigned max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned> dist(min, max);
    return dist(gen);
}

std::string generateSalt() {
    const int saltLength = 16; // 16 bytes = 128 bits
    unsigned char salt[saltLength];
    if (RAND_bytes(salt, saltLength) != 1) {
        unsigned long errCode = ERR_get_error();
        char errBuffer[256];
        ERR_error_string_n(errCode, errBuffer, sizeof(errBuffer));
        std::cout << "Failed to generate salt: " << errBuffer << std::endl;
        return "";
    }

    std::ostringstream saltStream;
    for (int i = 0; i < saltLength; ++i) {
        saltStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(salt[i]);
    }
    return saltStream.str();
}

std::string hashString(const std::string& str, const std::string& salt) {
    std::string combined = str + salt;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, combined.c_str(), combined.size());
    SHA256_Final(hash, &sha256);

    std::ostringstream result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return result.str();
}

bool tryPassDouble(const std::string& s, double& d) {
    const char* str = s.c_str();
    char* end;
    d = std::strtod(str, &end);
    return end != str && *end == '\0';
}

bool tryPassInt(const std::string& s, int& i) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), i);
    return ec == std::errc() && ptr == s.data() + s.size();
}

bool tryPassUInt(const std::string& s, unsigned int& i) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), i);
    return ec == std::errc() && ptr == s.data() + s.size();
}

bool tryPassULong(const std::string& s, unsigned long& i) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), i);
    return ec == std::errc() && ptr == s.data() + s.size();
}

bool isValidString(const std::string& str) {
    for (char c : str) {
        if (!std::isprint(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

bool checkPassword(const std::string& password) {
    if (password.length() < 12) {
        return false;
    }

    bool hasUpper = false;
    bool hasLower = false;
    bool hasDigit = false;
    bool hasSpecial = false;

    for (char c : password) {
        if (std::isupper(c)) hasUpper = true;
        else if (std::islower(c)) hasLower = true;
        else if (std::isdigit(c)) hasDigit = true;
        else if (std::ispunct(c)) hasSpecial = true;
    }

    if (hasUpper && hasLower && hasDigit && hasSpecial) {
        return true;
    } else {
        return false;
    }
}

std::string getFirstParam(std::string& message) {
    size_t pos = message.find(static_cast<char>(30));
    
    if (pos == std::string::npos) {
        std::string result = message;
        message.clear();
        return result;
    }
    
    std::string firstParam = message.substr(0, pos);
    
    message.erase(0, pos + 1);
    
    return firstParam;
}


bool isLocked(std::mutex& m) {
    std::unique_lock<std::mutex> lock(m, std::try_to_lock);
    if (lock.owns_lock()) {
        return false;
    } else {
        return true;
    }
}


}
