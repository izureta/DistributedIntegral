#include <iostream>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <mutex>

using namespace std::chrono_literals;


#define BROADCAST_PORT 8001
#define TCP_PORT 9001
#define BUFFER_SIZE 256
#define MESSAGE_TIMEOUT 15
#define DELTA 0.0001


int masterSocket = -1;
std::atomic<bool> stopConnectingToMaster(false);
std::mutex mtx;

double f(double x) {
    return x;
}


int CreateUDPSocket() {
    int udpSocket;

    udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) {
        perror("UDP socket creation failed");
        //throw std::runtime_error("UDP socket creation failed");
    }
    int broadcastEnable = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, &broadcastEnable, sizeof(broadcastEnable));

    return udpSocket;
}

struct sockaddr_in CreateUDPAdress() {
    struct sockaddr_in broadcastAddr;

    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    broadcastAddr.sin_port = htons(BROADCAST_PORT);

    return broadcastAddr;
}


void ConnectToMaster() {
    int udpSocket = CreateUDPSocket();

    struct sockaddr_in broadcastAddr = CreateUDPAdress();
    
    while (bind(udpSocket, (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
        perror("Bind failed");
        std::this_thread::sleep_for(1ms);
        continue;
    }

    char buffer[BUFFER_SIZE];
    while (!stopConnectingToMaster) {
        std::this_thread::sleep_for(1ms);
        std::lock_guard<std::mutex> lock(mtx);
        if (masterSocket != -1) {
            continue;
        }

        socklen_t addrlen = sizeof(broadcastAddr);
        int len = recvfrom(udpSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&broadcastAddr, &addrlen);
        if (len > 0) {
            std::cout << "Received broadcast" << std::endl;
            buffer[len] = '\0';
            if (strcmp(buffer, "DISCOVER") == 0) {
                std::cout << "Broadcast message received: " << buffer << std::endl;

                int tcpSocket;
                struct sockaddr_in serverAddr;

                tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (tcpSocket < 0) {
                    perror("TCP socket creation failed");
                    continue;
                }

                memset(&serverAddr, 0, sizeof(serverAddr));
                serverAddr.sin_family = AF_INET;
                serverAddr.sin_port = htons(TCP_PORT);
                serverAddr.sin_addr = broadcastAddr.sin_addr;

                if (connect(tcpSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
                    perror("TCP connect failed");
                    close(tcpSocket);
                    continue;
                }

                std::cout << "Connected to master" << std::endl;
                masterSocket = tcpSocket;
            }
        }
    }
    close(udpSocket);
}

double RangeIntergration(double l, double r) {
    double result;
    for (double x = l; x < r; x += DELTA) {
        result += DELTA * f(x);
    }
    return result;
}

void CalculateRangeIntegral() {
    struct timeval timeout;
    timeout.tv_sec = MESSAGE_TIMEOUT;
    timeout.tv_usec = 0;

    while (true) {
        std::this_thread::sleep_for(1ms);
        std::lock_guard<std::mutex> lock(mtx);
        if (masterSocket == -1) {
            continue;
        }
        if (setsockopt(masterSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
            std::cerr << "Receive timeout setting error: " << strerror(errno) << std::endl;
            close(masterSocket);
            masterSocket = -1;
            continue;
        }
        std::pair<double, double> range;
        if (recv(masterSocket, &range, sizeof(range), 0) <= 0) {
            std::cerr << "Range receiving error or timeout: " << strerror(errno) << std::endl;
            close(masterSocket);
            masterSocket = -1;
            continue;
        }

        std::cout << "Got range: " << range.first << ' ' << range.second << std::endl; 
        double rangeIntergrationResult = RangeIntergration(range.first, range.second);

        if (setsockopt(masterSocket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
            std::cerr << "Send timeout setting error: " << strerror(errno) << std::endl;
            close(masterSocket);
            masterSocket = -1;
            continue;
        }

        if (send(masterSocket, &rangeIntergrationResult, sizeof(rangeIntergrationResult), 0) != sizeof(rangeIntergrationResult)) {
            std::cerr << "Result sending error or timeout: " << strerror(errno) << std::endl;
            close(masterSocket);
            masterSocket = -1;
            continue;
        }
    }
}


int main() {
    std::thread connectToMaster(ConnectToMaster);
    CalculateRangeIntegral();

    stopConnectingToMaster = 1;
    connectToMaster.join();
    return 0;
}
