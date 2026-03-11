#pragma once

#include <enet/enet.h>

#include <string>

class Network {
public:
  Network(const char *server);
  ~Network();
  void send(std::string_view msg);
  std::string receive(unsigned int timeout = 0);

private:
  ENetHost *client;
  ENetPeer *peer;
  ENetEvent event;
};
