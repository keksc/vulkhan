#include <enet/enet.h>

#include <map>
#include <print>

#include "packet.hpp"

int main() {
  if (enet_initialize() != 0) {
    std::println("Failed to initialize ENet");
    return 1;
  }

  atexit(enet_deinitialize);

  ENetAddress address{};
  enet_address_set_host(&address, "0.0.0.0");
  address.port = 1234;


  ENetHost *server = enet_host_create(&address, 32, 2, 0, 0);

  if (!server) {
    std::println("Failed to create ENet server");
    return 1;
  }

  std::println("Server started on port 1234");

  std::map<uint32_t, ENetPeer *> clients;
  ENetEvent event{};

  while (true) {
    while (enet_host_service(server, &event, 1000) > 0) {
      switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        uint32_t newId = event.peer->connectID;
        std::println("Client {} connected", newId);

        // Tell everyone else this player joined
        Packet joinPkt{PacketType::Join, newId};
        ENetPacket *p = enet_packet_create(&joinPkt, sizeof(joinPkt),
                                           ENET_PACKET_FLAG_RELIABLE);
        enet_host_broadcast(server, 0, p);

        // Tell the new player about everyone currently online
        for (const auto &[id, peer] : clients) {
          Packet existPkt{PacketType::Join, id};
          ENetPacket *ep = enet_packet_create(&existPkt, sizeof(existPkt),
                                              ENET_PACKET_FLAG_RELIABLE);
          enet_peer_send(event.peer, 0, ep);
        }

        clients[newId] = event.peer;
        break;
      }

      case ENET_EVENT_TYPE_RECEIVE: {
        if (event.packet->dataLength >= sizeof(Packet)) {
          Packet *basePkt = reinterpret_cast<Packet *>(event.packet->data);

          if (basePkt->type == PacketType::Update) {
            // Ensure the packet has the correct sender ID to prevent spoofing
            basePkt->id = event.peer->connectID;

            for (auto &[id, peer] : clients) {
              if (peer != event.peer) {
                ENetPacket *fwd = enet_packet_create(
                    event.packet->data, event.packet->dataLength,
                    ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
                enet_peer_send(peer, 0, fwd);
              }
            }
          }
        }
        enet_packet_destroy(event.packet);
        break;
      }

      case ENET_EVENT_TYPE_DISCONNECT: {
        uint32_t leftId = event.peer->connectID;
        std::println("Client {} disconnected", leftId);
        clients.erase(leftId);

        Packet leavePkt{PacketType::Leave, leftId};
        ENetPacket *p = enet_packet_create(&leavePkt, sizeof(leavePkt),
                                           ENET_PACKET_FLAG_RELIABLE);
        enet_host_broadcast(server, 0, p);
        break;
      }

      default:
        break;
      }
    }
  }

  enet_host_destroy(server);
  return 0;
}
