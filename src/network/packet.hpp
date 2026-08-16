#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

enum class PacketType : uint8_t {
  // Broadcast everyone about who disconnected
  Leave = 0,
  // No ID, will find it, sent by client
  ClientUpdate = 1,
  // Handshake
  Hello = 2,
  // For broadcasting updates with found IDs
  ServerUpdate = 3,
  // Useless bullshit
  Spawn = 4,
};

#pragma pack(push, 1)
struct Packet {
  PacketType type;
};

// Client -> server. No id: enet already tells the server which peer this is.
struct ClientUpdatePacket : public Packet {
  glm::vec3 position;
  glm::quat orientation;
};

// Server -> client broadcast. Has id: it's telling us about someone else.
struct ServerUpdatePacket : public Packet {
  uint32_t id;
  glm::vec3 position;
  glm::quat orientation;
};

struct SpawnPacket : public Packet {
  glm::vec3 position;
  glm::quat orientation;
};

struct LeavePacket : public Packet {
  uint32_t id;
};

// Sent exactly once by the client, immediately after connecting: carries our
// persistent client UUID so the server can identify us. The server ignores
// (drops) every other packet from a peer until this arrives, and the client
// never needs to send it again. Never sent by the server -- client-to-server
// only.
struct HelloPacket : Packet {
  uint8_t uuid[16];
};
#pragma pack(pop)
