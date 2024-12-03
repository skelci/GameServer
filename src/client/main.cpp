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

    NetworkClient client2(server, 8082);

    client2.Start();

    while (true) {
        std::string data;
        std::getline(std::cin, data);

        std::replace(data.begin(), data.end(), ' ', (char)30);

        if (data == "exit") {
            break;
        }

        client2.SendMessage(data);
    }

    client2.Stop();
}

void testNetworkClient() {
    std::string server = "127.0.0.1";
    unsigned short port = 8083;

    std::srand(std::time(nullptr));
    int test = std::rand() % 10;

    NetworkClient client(server, port);

    try {
        switch (test) {
            case 0:
                client.Start();
                client.SendMessage("REGISTER_PREPARE", "test", "test");
                client.Stop();
                break;

            case 1:
                client.Start();
                client.SendMessage("REGISTER_PREPARE", "test", "qwerQWER12341!\"#$", "123456");
                client.SendMessage("REGISTER", "123456"); 
                client.Stop();
                break;

            case 2:
                client.Start();
                client.SendMessage("LOGIN_PREPARE", "test", "qwerQWER12341!\"#$");
                client.SendMessage("LOGIN", "123456");
                client.Stop();
                break;

            case 3:
                client.Start();
                client.SendMessage("REGISTER_PREPARE", "test", "qwerQWER12341!\"#$", "123456");
                client.SendMessage("LOGIN", "123456"); 
                client.Stop();
                break;

            case 4:
                client.Start();
                client.SendMessage("LOGIN_PREPARE", "test", "qwerQWER12341!\"#$");
                client.SendMessage("REGISTER", "123456");
                client.Stop();
                break;

            case 5:
                client.Start();
                client.SendMessage("LOGIN", "123456");
                client.Stop();
                break;

            case 6:
                client.Start();
                client.SendMessage("REGISTER", "123456");
                client.Stop();
                break;

            case 7:
                client.Start();
                client.SendMessage("UNKNOWN", "hehe");
                client.Stop();
                break;
            
            case 8:
                client.Start();
                client.SendMessage("LOGIN_PREPARE");
                client.Stop();
                break;

            case 9:
                client.Start();
                client.SendMessage("REGISTER_PREPARE");
                client.Stop();
                break;
            }

            testNetworkClient();
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;

        testNetworkClient();
    }
}

void RunTests(int threads){
    for (int i = 0; i < threads; i++) {
        std::thread(testNetworkClient).detach();
    }
    std::this_thread::sleep_for(std::chrono::seconds(60));
}

int main(){

    networkTemp();
    // RunTests(1000);

    return 0;
}
