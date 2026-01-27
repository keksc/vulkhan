#pragma once

#include <glm/geometric.hpp>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../audio.hpp"
#include "../../engineContext.hpp"
#include "../../image.hpp"
#include "../../scene.hpp"
#include "../skybox.hpp"
#include "../system.hpp"
#include "WSTessendorf.hpp"

namespace vkh {
class WaterSys : public System {
public:
  WaterSys(EngineContext &context, SkyboxSys &skyboxSys);
  ~WaterSys();
  void prepare();
  void createRenderData();
  void render();
  void update();

private:
  SkyboxSys &skyboxSys;
  struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;

    Vertex(const glm::vec3 &position, const glm::vec2 &texCoord)
        : pos(position), uv(texCoord) {}

    constexpr static VkVertexInputBindingDescription GetBindingDescription() {
      return VkVertexInputBindingDescription{.binding = 0,
                                             .stride = sizeof(Vertex),
                                             .inputRate =
                                                 VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::vector<VkVertexInputAttributeDescription>
    GetAttributeDescriptions() {
      return std::vector<VkVertexInputAttributeDescription>{
          {.location = 0,
           .binding = 0,
           .format = VK_FORMAT_R32G32B32_SFLOAT,
           .offset = offsetof(Vertex, pos)},
          {.location = 1,
           .binding = 0,
           .format = VK_FORMAT_R32G32_SFLOAT,
           .offset = offsetof(Vertex, uv)}};
    }

    static const inline std::vector<VkVertexInputBindingDescription>
        s_BindingDescriptions{GetBindingDescription()};

    static const inline std::vector<VkVertexInputAttributeDescription>
        s_AttribDescriptions{GetAttributeDescriptions()};
  };
  static constexpr VkFormat mapFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
  struct FrameMapData {
    std::unique_ptr<Image> displacementMap{nullptr};
    std::unique_ptr<Image> normalMap{nullptr};
  };
  uint32_t tileResolution{32};
  float vertexDistance{1000.f / static_cast<float>(tileResolution)};

  VkDescriptorSetLayout setLayout;
  std::vector<VkDescriptorSet> sets;
  std::vector<Buffer<std::byte>> uniformBuffers;
  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Scene<Vertex>> scene;

  WSTessendorf modelTess;
  FrameMapData frameMap;

  bool playAnimation{true};
  float animSpeed{3.0};

  static const inline glm::vec3 s_kWavelengthsRGB_m{680e-9, 550e-9, 440e-9};
  static const inline glm::vec3 s_kWavelengthsRGB_nm{680, 550, 440};

  uint32_t m_WaterTypeCoefIndex{0};
  uint32_t m_BaseScatterCoefIndex{0};

  static glm::vec3 ComputeScatteringCoefPA01(float b_lambda0) {
    return b_lambda0 * ((-0.00113f * s_kWavelengthsRGB_nm + 1.62517f) /
                        (-0.00113f * 514.0f + 1.62517f));
  }

  /**
   * @param b Scattering coefficient for each wavelength, in m^-1
   * @return Backscattering coefficient for each wavelength, from [PA01]
   */
  static glm::vec3 ComputeBackscatteringCoefPA01(const glm::vec3 &b) {
    return 0.01829f * b + 0.00006f;
  }

  /**
   * @param C pigment concentration for an open water type, [mg/m^3]
   * @return Backscattering coefficient based on eqs.:24,25,26 [PA01]
   */
  static glm::vec3 ComputeBackscatteringCoefPigmentPA01(float C) {
    // Morel. Optical modeling of the upper ocean in relation to its biogenus
    //  matter content.
    const glm::vec3 b_w(0.0007f, 0.00173f, 0.005f);

    // Ratio of backscattering and scattering coeffiecients of the pigments
    const glm::vec3 B_b =
        0.002f + 0.02f *
                     (0.5f - 0.25f * ((1.0f / glm::log(10.0f)) * glm::log(C))) *
                     (550.0f / s_kWavelengthsRGB_nm);
    // Scattering coefficient of the pigment
    const float b_p = 0.3f * glm::pow(C, 0.62f);

    return 0.5f * b_w + B_b * b_p;
  }

  struct VertexUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    float WSHeightAmp;
    float WSChoppy;
    float scale{1.0f};    ///< Texture scale
    float vertexDistance; ///< Physical distance between vertices
  } vertexUBO{};

  struct WaterSurfaceUBO {
    alignas(16) glm::vec3 camPos{};
  } waterSurfaceUBO;

  /**
   * @pre size > 0
   * @return Size 'size' aligned to 'alignment'
   */
  static size_t alignSizeTo(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
  }

  const uint32_t totalVertexCount = (tileResolution + 1) * (tileResolution + 1);
  const uint32_t indicesPerQuad = 4;
  const uint32_t totalIndexCount =
      tileResolution * tileResolution * indicesPerQuad;

  void createPipeline();
  void createUniformBuffers(const uint32_t bufferCount);
  void createDescriptorSets(const uint32_t count);
  void recordCreateFrameMaps(VkCommandBuffer cmd);
  void recordUpdateFrameMaps(VkCommandBuffer cmd, FrameMapData &frame);
  std::vector<Vertex> createGridVertices();
  std::vector<uint32_t> createGridIndices();
  void updateDescriptorSet(VkDescriptorSet set);
  void updateUniformBuffer();
  void createDescriptorSetLayout();
  void createMesh();

  audio::Sound oceanSound;
};
} // namespace vkh
