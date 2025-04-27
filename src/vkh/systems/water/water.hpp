#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../engineContext.hpp"
#include "../../image.hpp"
#include "../../mesh.hpp"
#include "../system.hpp"
#include "WSTessendorf.hpp"
#include "skyPreetham.hpp"

namespace vkh {
class WaterSys : public System {
public:
  struct SkyParams {
    alignas(16) glm::vec3 sunColor{1.0};
    float sunIntensity{1.0};
    SkyPreetham::Props props{};
  };

  WaterSys(EngineContext &context);
  ~WaterSys();
  void prepare();
  void createRenderData();
  void render();
  void update(const SkyParams &skyParams);

private:
  void createSampler();

  VkSampler sampler;
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
  static constexpr bool useMipMapping = false;
  struct FrameMapData {
    std::unique_ptr<Image> displacementMap{nullptr};
    std::unique_ptr<Image> normalMap{nullptr};
  };
  static const uint32_t maxTileSize{512};
  uint32_t m_TileSize{maxTileSize};
  float m_VertexDistance{1000.f /
                         static_cast<float>(maxTileSize)};

  std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
  std::vector<VkDescriptorSet> descriptorSets;
  std::vector<std::unique_ptr<Buffer>> uniformBuffers;
  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Mesh<Vertex>> mesh;

  WSTessendorf modelTess;
  FrameMapData frameMap;
  std::unique_ptr<Buffer> stagingBuffer;

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
    float scale{1.0f}; ///< Texture scale
  } vertexUBO{};

  struct WaterSurfaceUBO {
    alignas(16) glm::vec3 camPos{};
    float height{50.f};
    alignas(16) glm::vec3 absorpCoef{glm::vec3{.420f, .063f, .019f}};
    alignas(16) glm::vec3 scatterCoef{ComputeScatteringCoefPA01(.037f)};
    alignas(16) glm::vec3 backscatterCoef{
        ComputeBackscatteringCoefPA01(scatterCoef)};
    // -------------------------------------------------
    alignas(16) glm::vec3 terrainColor{.964f, 1.f, .824f};
    float skyIntensity{1.f};
    float specularIntensity{1.f};
    float specularHighlights{32.f};
    SkyParams sky;
  } waterSurfaceUBO;

  /**
   * @pre size > 0
   * @return Size 'size' aligned to 'alignment'
   */
  static size_t alignSizeTo(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
  }

  uint32_t getTotalVertexCount(const uint32_t kTileSize) {
    return (kTileSize + 1) * (kTileSize + 1);
  }

  uint32_t getTotalIndexCount(const uint32_t totalVertexCount) {
    const uint32_t indicesPerTriangle = 3, trianglesPerQuad = 2;
    return totalVertexCount * indicesPerTriangle * trianglesPerQuad;
  }

  static uint32_t getMaxVertexCount() {
    return (maxTileSize + 1) * (maxTileSize + 1);
  }
  static uint32_t getMaxIndexCount() {
    const uint32_t indicesPerTriangle = 3, trianglesPerQuad = 2;
    return getMaxVertexCount() * indicesPerTriangle * trianglesPerQuad;
  }

  void createPipeline();
  void createUniformBuffers(const uint32_t bufferCount);
  void createDescriptorSets(const uint32_t count);
  void createFrameMaps(VkCommandBuffer cmdBuffer);
  void updateFrameMaps(VkCommandBuffer cmdBuffer, FrameMapData &frame);
  void copyModelTessDataToStagingBuffer();
  void prepareModelTess(VkCommandBuffer cmdBuffer);
  std::vector<Vertex> createGridVertices(const uint32_t kTileSize,
                                         const float kScale);
  std::vector<uint32_t> createGridIndices(const uint32_t kTileSize);
  void updateDescriptorSet(VkDescriptorSet set);
  void updateUniformBuffer();
  void createDescriptorSetLayout();
  void createMesh();
  void createStagingBuffer();
};
} // namespace vkh
