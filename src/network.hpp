#pragma once

#include <enet/enet.h>

#include <string>

class Network {
public:
  Network();
  ~Network();
  void send(std::string msg);
  std::string receive();

private:
  ENetHost *client;
  ENetPeer *peer;
  ENetEvent event;
};
