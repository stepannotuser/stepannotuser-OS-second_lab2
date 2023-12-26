#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace std;

struct Client {
    int connfd;
    sockaddr_in addr;
};

volatile sig_atomic_t signalReceived = 0;

void handleSignal(int sig) {
    signalReceived = 1;
}

void setupSignalHandler(sigset_t *originalMask) {
    struct sigaction sa;
    sigaction(SIGHUP, nullptr, &sa);
    sa.sa_handler = handleSignal;
    sa.sa_flags |= SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);

    sigset_t blockedMask;
    sigemptyset(&blockedMask);
    sigaddset(&blockedMask, SIGHUP);
    sigprocmask(SIG_BLOCK, &blockedMask, originalMask);
}

int createServer(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    sockaddr_in servaddr{};
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&servaddr), sizeof(servaddr)) != 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 5) != 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int main() {
    int sockfd = createServer(5005);
    cout << "Server is listening...\n";

    vector<Client> clients;
    char buffer[1024] = {0};

    sigset_t origSigMask;
    setupSignalHandler(&origSigMask);

    while (true) {
        if (signalReceived) {
            signalReceived = 0;
            cout << "Connected Clients: ";
            for (const auto &client : clients) {
                cout << "[" << inet_ntoa(client.addr.sin_addr) << ":" << htons(client.addr.sin_port) << "] ";
            }
            cout << "\n";
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        int maxFd = sockfd;

        for (const auto &client : clients) {
            FD_SET(client.connfd, &fds);
            if (client.connfd > maxFd) {
                maxFd = client.connfd;
            }
        }

        if (pselect(maxFd + 1, &fds, nullptr, nullptr, nullptr, &origSigMask) < 0 && errno != EINTR) {
            perror("pselect failed");
            return EXIT_FAILURE;
        }

        if (FD_ISSET(sockfd, &fds) && clients.size() < 3) {
            clients.emplace_back();
            auto &client = clients.back();
            socklen_t len = sizeof(client.addr);
            client.connfd = accept(sockfd, reinterpret_cast<sockaddr*>(&client.addr), &len);
            if (client.connfd >= 0) {
                cout << "[" << inet_ntoa(client.addr.sin_addr) << ":" << htons(client.addr.sin_port) << "] Connected!\n";
            } else {
                perror("Accept error");
                clients.pop_back();
            }
        }

        for (auto it = clients.begin(); it != clients.end(); ) {
            auto &client = *it;
            if (FD_ISSET(client.connfd, &fds)) {
                int readLen = read(client.connfd, buffer, sizeof(buffer) - 1);
                if (readLen > 0) {
                    buffer[readLen - 1] = 0;
                    cout << "[" << inet_ntoa(client.addr.sin_addr) << ":" << htons(client.addr.sin_port) << "] " << buffer << "\n";
                } else {
                    close(client.connfd);
                    cout << "[" << inet_ntoa(client.addr.sin_addr) << ":" << htons(client.addr.sin_port) << "] Connection closed\n";
                    it = clients.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    return 0;
}
