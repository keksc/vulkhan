#include "network.hpp"
#include <enet/enet.h>
#include <print>
#include <stdexcept>
#include <cstring>

Network::Network(const char* server) {
  if (enet_initialize() != 0) {
    throw std::runtime_error("Failed to initialize ENet");
  }

  client = enet_host_create(nullptr, 1, 2, 0, 0);

  if (!client) {
    throw std::runtime_error("Failed to create ENet client");
  }

  ENetAddress address{};
  enet_address_set_host(&address, server);
  address.port = 1234;

  peer = enet_host_connect(client, &address, 2, 0);
  if (!peer) {
    throw std::runtime_error("No available peers for connection");
  }

  if (enet_host_service(client, &event, 5000) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT) {
    std::println("Connected to server");
  } else {
    enet_peer_reset(peer);
    throw std::runtime_error("Connection to server failed");
  }
  enet_host_flush(client);
}

void Network::send(const void* data, size_t length) {
  ENetPacket *packet = enet_packet_create(data, length, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
  enet_peer_send(peer, 0, packet);
  enet_host_flush(client);
}

bool Network::receive(std::vector<uint8_t>& outData, unsigned int timeout) {
  while (enet_host_service(client, &event, timeout) > 0) {
    if (event.type == ENET_EVENT_TYPE_RECEIVE) {
      outData.resize(event.packet->dataLength);
      std::memcpy(outData.data(), event.packet->data, event.packet->dataLength);
      enet_packet_destroy(event.packet);
      return true; // Return true on first successful read
    } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
      std::println("Server closed the connection.");
    }
  }
  return false;
}

Network::~Network() {
  enet_peer_disconnect(peer, 0);

  std::vector<uint8_t> dump;
  receive(dump);
  
  while (enet_host_service(client, &event, 3000) > 0) {
    if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
      std::println("Disconnected cleanly");
      break;
    }
  }

  enet_host_destroy(client);
  enet_deinitialize();
}
