#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "network/NetworkClient.hpp"


void networkTemp(){
    std::string server = "127.0.0.1";
    unsigned short port = 8083;

    NetworkClient client(server, port);

    client.Start();

    while (true) {
        std::string data;
        std::getline(std::cin, data);

        std::replace(data.begin(), data.end(), ' ', (char)30);

        if (data == "exit") {
            break;
        }

        client.SendMessage(data);
    }

    client.Stop();
}

int main(){

    networkTemp();

    return 0;
}
