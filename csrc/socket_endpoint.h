#pragma once
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <string>

class SocketEndpoint
{
public:
  SocketEndpoint() = delete;
  SocketEndpoint(int port);
  SocketEndpoint(const std::string &serverName, int port);
  ~SocketEndpoint()
  {
    close(sockfd_);
  }

  int syncData(size_t size, const void *outBuf, void *inBuf);
  int syncReady();

private:
  int startListening(int port);
  void logError(const std::string &message);
  void logDebug(const std::string &message);
  int sockfd_;
  int isDaemon_;
};