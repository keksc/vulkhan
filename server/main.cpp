#include <enet/enet.h>

#include <cstring>
#include <print>

int main() {
  if (enet_initialize() != 0) {
    std::println("Failed to initialize ENet");
    return 1;
  }

  atexit(enet_deinitialize);

  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = 1234;

  ENetHost *server = enet_host_create(&address,
                                      32, // max clients
                                      2,  // channels
                                      0, 0);

  if (!server) {
    std::println("Failed to create ENet server");
    return 1;
  }

  std::println("Server started on port 1234");

  ENetEvent event{};
  while (true) {
    while (enet_host_service(server, &event, 1000) > 0) {
      switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        std::println("Client connected from {}:{}", event.peer->address.host,
                     event.peer->address.port);
        break;

      case ENET_EVENT_TYPE_RECEIVE: {
        std::string_view msg(reinterpret_cast<char *>(event.packet->data),
                             event.packet->dataLength);

        std::println("Message received: {}", msg);

        const char *reply = "Hello from server!";
        ENetPacket *packet = enet_packet_create(reply, std::strlen(reply) + 1,
                                                ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
        enet_peer_send(event.peer, 0, packet);
        enet_host_flush(server);
        enet_packet_destroy(event.packet);
        break;
      }

      case ENET_EVENT_TYPE_DISCONNECT:
        std::println("Client disconnected");
        break;

      default:
        break;
      }
    }
  }

  enet_host_destroy(server);
  return 0;
}
