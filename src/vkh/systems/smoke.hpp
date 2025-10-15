#pragma once

#include "../buffer.hpp"
#include "system.hpp"

class FluidGrid {
  std::vector<float> velocitiesX;
  std::vector<float> velocitiesY;

  float cellSize;

public:
  glm::ivec2 cellCount;
  FluidGrid(glm::ivec2 cellCount, float cellSize)
      : cellCount{cellCount}, cellSize{cellSize},
        velocitiesX((cellCount.x + 1) * cellCount.y),
        velocitiesY(cellCount.x * (cellCount.y + 1)) {}

  inline float &velX(int i, int j) { return velocitiesX[i * cellCount.y + j]; }

  inline float &velY(int i, int j) {
    return velocitiesY[i * (cellCount.y + 1) + j];
  }

  inline const float &velX(int i, int j) const {
    return velocitiesX[i * cellCount.y + j];
  }

  inline const float &velY(int i, int j) const {
    return velocitiesY[i * (cellCount.y + 1) + j];
  }
  float calculateVelocityDivergence(glm::ivec2 cell) {
    // float velocityTop = velY[cell.x + ];
    // float velocityLeft = velocitiesY[cell.x, cell.y + 1];
    // float velocityRight = velocitiesY[cell.x, cell.y + 1];
    // float velocityBottom = velocitiesY[cell.x, cell.y + 1];
    return 1.f;
  }
};

namespace vkh {
class SmokeSys : public System {
public:
  SmokeSys(EngineContext &context);
  void update();
  void render();

  struct Vertex {
    glm::vec2 pos{};
    glm::vec3 col{};

    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(Vertex);
      bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescriptions;
    }
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(Vertex, col));

      return attributeDescriptions;
    }
  };

  struct Cell {
    glm::vec3 color{};
  };
  std::vector<Cell> grid;
  FluidGrid fluidGrid;

private:
  void createPipeline();
  void createBuffer();

  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Buffer<Vertex>> vertexBuffer;
}; // namespace particlesSys
} // namespace vkh
