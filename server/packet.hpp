#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

enum class PacketType : uint8_t { Join, Leave, Update };

#pragma pack(push, 1)
struct Packet {
  PacketType type;
  uint32_t id;
};

struct UpdatePacket : public Packet {
  glm::vec3 position;
  glm::quat orientation;
};
#pragma pack(pop)
