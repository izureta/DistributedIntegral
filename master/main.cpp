#include <iostream>
#include <cstring>
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
#include <chrono>

using namespace std::chrono_literals;

#define BROADCAST_PORT 8001
#define TCP_PORT 9001
#define BROADCAST_INTERVAL 2
#define MESSAGE_TIMEOUT 10
#define L 0
#define R 100


std::atomic<bool> stopDiscovering(false);
std::atomic<bool> stopUpdatingWorkers(false);
double integrationResult = 0;
std::vector<std::pair<double, double> > ranges;
std::vector<int> availableWorkers;
std::mutex mtx;
std::mutex resultMtx;

int CreateUDPSocket() {
    int udpSocket;

    udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) {
        perror("UDP socket creation failed");
        throw std::runtime_error("UDP socket creation failed");
    }
    return udpSocket;
}

struct sockaddr_in CreateUDPAddress(int udpSocket) {
    struct sockaddr_in broadcastAddr;

    int broadcastEnable = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = (in_port_t)htons(BROADCAST_PORT);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    return broadcastAddr;
}


void DiscoverWorkers() {
    int udpSocket = CreateUDPSocket();

    auto broadcastAddr = CreateUDPAddress(udpSocket);

    char message[] = "DISCOVER";

    while (!stopDiscovering) {
        std::cout << "Sending broadcast" << std::endl;
        sendto(udpSocket, message, strlen(message), 0, (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));
        std::this_thread::sleep_for(BROADCAST_INTERVAL * 1s);
    }
    close(udpSocket);
}

int CreateTCPSocket() {
    int tcpSocket;
    struct sockaddr_in tcpAddr;

    tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpSocket < 0) {
        perror("TCP socket creation failed");
        throw std::runtime_error("TCP socket creation failed");
    }

    memset(&tcpAddr, 0, sizeof(tcpAddr));
    tcpAddr.sin_family = AF_INET;
    tcpAddr.sin_addr.s_addr = INADDR_ANY;
    tcpAddr.sin_port = htons(TCP_PORT);

    if (bind(tcpSocket, (struct sockaddr *)&tcpAddr, sizeof(tcpAddr)) < 0) {
        perror("TCP bind failed");
        throw std::runtime_error("TCP bind failed");
    }

    if (listen(tcpSocket, 200) < 0) {
        perror("Listen failed");
        throw std::runtime_error("TCP listen failed");
    }

    return tcpSocket;
}

void AcceptNewConnections() {
    int tcpSocket = CreateTCPSocket();

    struct sockaddr_in clientAddr;

    struct timeval timeout;
    timeout.tv_sec = BROADCAST_INTERVAL;
    timeout.tv_usec = 0;

    int max_sd = tcpSocket;

    while (!stopUpdatingWorkers) {
        fd_set readfds;
        fd_set writefds;

        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        FD_SET(tcpSocket, &readfds);

        {
            std::this_thread::sleep_for(1ms);
            std::lock_guard<std::mutex> lock(mtx);
            for (int socket : availableWorkers) {
                FD_SET(socket, &readfds);
                FD_SET(socket, &writefds);
                max_sd = std::max(max_sd, socket);
            }

            int activity = select(max_sd + 1, &readfds, nullptr, nullptr, &timeout);
            if ((activity < 0) && (errno != EINTR)) {
                perror("Select error");
            }

            if (FD_ISSET(tcpSocket, &readfds)) {
                socklen_t addrlen = sizeof(clientAddr);
                int newSocket = accept(tcpSocket, (struct sockaddr *)&clientAddr, &addrlen);
                if (newSocket >= 0) {
                    if (std::find(availableWorkers.begin(), availableWorkers.end(), newSocket) == availableWorkers.end()) {
                        availableWorkers.push_back(newSocket);
                    }
                    std::cout << "New connection established from " << inet_ntoa(clientAddr.sin_addr) << std::endl;
                }
            }
        }
    }
    close(tcpSocket);
}

void CalculateIntegral() {
    struct timeval timeout;
    timeout.tv_sec = MESSAGE_TIMEOUT;
    timeout.tv_usec = 0;

    while (!ranges.empty()) {
        std::this_thread::sleep_for(1ms);
        {
            std::lock_guard<std::mutex> lock(mtx);
            std::vector<bool> isDeadWorker(availableWorkers.size());
            std::vector<bool> calculatedRanges(ranges.size());
            std::vector<std::thread> queries;
            for (size_t i = 0; i < std::min(availableWorkers.size(), ranges.size()); ++i) {
                auto range = ranges[i];
                int socket = availableWorkers[i];
                queries.emplace_back([i, range, socket, timeout, &isDeadWorker, &calculatedRanges, &integrationResult]{
                    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
                        std::cerr << "Send timeout setting error: " << strerror(errno) << std::endl;
                        isDeadWorker[i] = true;
                        return;
                    }

                    if (send(socket, &range, sizeof(range), 0) != sizeof(range)) {
                        std::cerr << "Range sending error or timeout: " << strerror(errno) << std::endl;
                        isDeadWorker[i] = true;
                        return;
                    }

                    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
                        std::cerr << "Receive timeout setting error: " << strerror(errno) << std::endl;
                        isDeadWorker[i] = true;
                        return;
                    }

                    double result;
                    if (recv(socket, &result, sizeof(result), 0) <= 0) {
                        std::cerr << "Intergration result receiving error or timeout: " << strerror(errno) << std::endl;
                        isDeadWorker[i] = true;
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(resultMtx);
                        integrationResult += result;
                    }

                    calculatedRanges[i] = true;
                });
            }

            for (size_t i = 0; i < std::min(availableWorkers.size(), ranges.size()); ++i) {
                queries[i].join();
            }

            std::vector<int> stillAliveWorkers;
            std::vector<std::pair<double, double> > remainingRanges;
            for (size_t i = 0; i < availableWorkers.size(); ++i) {
                if (!isDeadWorker[i]) {
                    stillAliveWorkers.push_back(availableWorkers[i]);
                } else {
                    close(availableWorkers[i]);
                }
            }
            for (size_t i = 0; i < ranges.size(); ++i) {
                if (!calculatedRanges[i]) {
                    remainingRanges.push_back(ranges[i]);
                }
            }
            std::swap(stillAliveWorkers, availableWorkers);
            std::swap(remainingRanges, ranges);
        }
    }
}


int main() {
    sleep(10);
    for (int i = 0; i < 100; ++i) {
        ranges.emplace_back((double)L + (double)i * (R - L) / 100.0, (double)L + (double)(i + 1) * (R - L) / 100.0);
    }

    std::thread discoverWorkers(DiscoverWorkers);
    std::thread acceptNewConnections(AcceptNewConnections);

    CalculateIntegral();

    std::cout << "Integration result: " << integrationResult << std::endl;

    stopDiscovering = 1;
    discoverWorkers.join();
    stopUpdatingWorkers = 1;
    acceptNewConnections.join();

    for (int socket : availableWorkers) {
        close(socket);
    }
    return 0;
}
