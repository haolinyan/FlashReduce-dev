#include "socket_endpoint.h"

void SocketEndpoint::logError(const std::string& message) {
    return; // Placeholder for error logging
}

void SocketEndpoint::logDebug(const std::string& message) {
    return; // Placeholder for error logging
}

int SocketEndpoint::startListening(int port) {
    struct addrinfo *res, *t;
    struct addrinfo hints = {.ai_flags = AI_PASSIVE,
                             .ai_family = AF_UNSPEC,
                             .ai_socktype = SOCK_STREAM};
    char *service;
    int n;
    int sockfd = -1;

    if (asprintf(&service, "%d", port) < 0) {
        throw std::runtime_error("asprintf failed");
    }

    n = getaddrinfo(nullptr, service, &hints, &res);
    if (n < 0) {
        free(service);
        logError(std::string(gai_strerror(n)) + " for port " + std::to_string(port));
        return -1;
    }

    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            n = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);
            if (!bind(sockfd, t->ai_addr, t->ai_addrlen)) {
                logDebug("Socket bound to IP: " +
                         std::string(inet_ntoa(((struct sockaddr_in *)t->ai_addr)->sin_addr)) +
                         ", Port: " + std::to_string(ntohs(((struct sockaddr_in *)t->ai_addr)->sin_port)));
                break;
            }
            close(sockfd);
            sockfd = -1;
        }
    }
    freeaddrinfo(res);
    free(service);

    if (sockfd < 0) {
        throw std::runtime_error("Couldn't listen to port " + std::to_string(port));
    }

    logDebug("Listening on port " + std::to_string(port));
    listen(sockfd, 1);
    return sockfd;
}

SocketEndpoint::SocketEndpoint(int port) : isDaemon_(1) {
    int sockfd = startListening(port);
    int connfd = accept(sockfd, nullptr, nullptr);
    close(sockfd);

    if (connfd < 0) {
        throw std::runtime_error("accept() failed");
    }

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(connfd, (struct sockaddr *)&addr, &addr_len);
    logDebug("Connection accepted from IP: " + std::string(inet_ntoa(addr.sin_addr)) +
             ", Port: " + std::to_string(ntohs(addr.sin_port)));
    sockfd_ = connfd;
}

SocketEndpoint::SocketEndpoint(const std::string& serverName, int port) : isDaemon_(0) {
    int retry = 0;
    struct addrinfo *res, *t;
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
    char *service;
    int n;
    int sockfd = -1;

    if (asprintf(&service, "%d", port) < 0) {
        throw std::runtime_error("asprintf failed");
    }

    n = getaddrinfo(serverName.c_str(), service, &hints, &res);
    if (n < 0) {
        free(service);
        logError(std::string(gai_strerror(n)) + " for " + serverName + ":" + std::to_string(port));
        throw std::runtime_error("getaddrinfo failed");
    }

connect:
    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            if (!connect(sockfd, t->ai_addr, t->ai_addrlen)) {
                logDebug("Connected to server IP: " +
                         std::string(inet_ntoa(((struct sockaddr_in *)t->ai_addr)->sin_addr)) +
                         ", Port: " + std::to_string(ntohs(((struct sockaddr_in *)t->ai_addr)->sin_port)));
                break;
            }
            close(sockfd);
            sockfd = -1;
        }
    }
    if (sockfd < 0 && retry < 5) {
        retry++;
        logDebug("Couldn't connect to " + serverName + ":" + std::to_string(port) + ", retrying...");
        sleep(1);
        goto connect;
    }
    freeaddrinfo(res);
    free(service);

    if (sockfd < 0) {
        throw std::runtime_error("Couldn't connect to " + serverName + ":" + std::to_string(port));
    }

    sockfd_ = sockfd;
}

int SocketEndpoint::syncData(size_t size, const void* outBuf, void* inBuf) {
    int rc;
    if (isDaemon_) {
        rc = send(sockfd_, outBuf, size, 0);
        if (rc <= 0) return rc;
        rc = recv(sockfd_, inBuf, size, 0);
        if (rc <= 0) return rc;
    } else {
        rc = recv(sockfd_, inBuf, size, 0);
        if (rc <= 0) return rc;
        rc = send(sockfd_, outBuf, size, 0);
        if (rc <= 0) return rc;
    }
    return 0;
}

int SocketEndpoint::syncReady() {
    char cmBuf = 'a';
    return syncData(sizeof(cmBuf), &cmBuf, &cmBuf);
}