#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

// Must stay in this exact order/values -- matches data_utilities::PacketType
// on the Rust server (Join=0, Leave=1, Update=2, Hello=3).
enum class PacketType : uint8_t { Join, Leave, Update, Hello };

#pragma pack(push, 1)
struct Packet {
  PacketType type;
  uint32_t id;
};

// The Rust server's to_packet_bytes() serializes Join/Update as the full
// Packet struct (type + id + position + orientation, 33 bytes) but Leave as
// just the 5-byte header (type + id) -- there's no position/orientation to
// send when a player leaves. Always read the 5-byte header first and only
// read the rest for types that carry it (see NetworkSession::poll).
struct UpdatePacket : public Packet {
  glm::vec3 position;
  glm::quat orientation; // stored x, y, z, w -- matches glam::Quat::to_array()
};

// Sent exactly once by the client, immediately after connecting: carries our
// persistent client UUID so the server can identify us. The server ignores
// (drops) every other packet from a peer until this arrives, and the client
// never needs to send it again. Never sent by the server -- client-to-server
// only.
struct HelloPacket {
  PacketType type; // always PacketType::Hello
  uint8_t uuid[16];
};
#pragma pack(pop)

static_assert(sizeof(UpdatePacket) == 33,
              "UpdatePacket must be 33 bytes to match the Rust #[repr(C, packed)] UpdatePacket");
static_assert(sizeof(HelloPacket) == 17,
              "HelloPacket must be 17 bytes to match the Rust #[repr(C, packed)] HelloPacket");
