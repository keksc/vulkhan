#include "network.hpp"

#include <enet/enet.h>

#include <print>
#include <stdexcept>

Network::Network() {
  if (enet_initialize() != 0) {
    throw std::runtime_error("Failed to initialize ENet");
  }

  client = enet_host_create(nullptr, 1, 2, 0, 0);

  if (!client) {
    throw std::runtime_error("Failed to create ENet client");
  }

  ENetAddress address{};
  enet_address_set_host(&address, "127.0.0.1");
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
}
void Network::send(std::string msg) {
  ENetPacket *packet = enet_packet_create(msg.data(), msg.length() + 1,
                                          ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);

  enet_peer_send(peer, 0, packet);
  enet_host_flush(client);
}
std::string Network::receive() {
  std::string msg = "";
  // Use a 0 timeout so this doesn't freeze the game
  while (enet_host_service(client, &event, 0) > 0) {
    if (event.type == ENET_EVENT_TYPE_RECEIVE) {
      msg = std::string(reinterpret_cast<char *>(event.packet->data),
                        event.packet->dataLength);

      enet_packet_destroy(event.packet);
      return msg; // Return the first message found
    } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
      std::println("Server closed the connection.");
    }
  }
  return msg;
}
Network::~Network() {
  enet_peer_disconnect(peer, 0);

  while (enet_host_service(client, &event, 3000) > 0) {
    if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
      std::println("Disconnected cleanly");
      break;
    }
  }

  enet_host_destroy(client);
  enet_deinitialize();
}
