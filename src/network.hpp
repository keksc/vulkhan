#pragma once

#include <enet/enet.h>
#include <string>
#include <vector>

class Network {
public:
  Network(const char *server);
  ~Network();
  
  void send(const void* data, size_t length);
  bool receive(std::vector<uint8_t>& outData, unsigned int timeout = 0);

private:
  ENetHost *client;
  ENetPeer *peer;
  ENetEvent event;
};
