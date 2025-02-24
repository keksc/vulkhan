#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <memory>
#include <vector>

#include "../../engineContext.hpp"
#include "../../image.hpp"
#include "../../mesh.hpp"
#include "WSTessendorf.hpp"
#include "skyModel.hpp"

namespace vkh {
/**
 * EXPERIMENTAL
 *  FIXME: incorrect layout when changing to uninitialized frame map pair
 * without animation on
 *  TODO: should be on dedicated thread
 * @brief Enable for double buffered textures: two sets of textures, one used
 * for copying to, the other for rendering to, at each frame they are swapped.
 * For the current use-case, the performance might be slightly worse.
 */
// #define DOUBLE_BUFFERED

/**
 * @brief Represents a water surface rendered as a mesh.
 *
 */
class WaterSurfaceMesh {
public:
  struct Vertex;

  static const uint32_t s_kMinTileSize{16};
  static const uint32_t s_kMaxTileSize{1024};

public:
  struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec3 normal;

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
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // =========================================================================
  /**
   * @brief Creates vertex and index buffers to accomodate maximum size of
   *  vertices and indices.
   *  To render the mesh, fnc "Prepare()" must be called with the size of tile
   */
  WaterSurfaceMesh(EngineContext &context);
  ~WaterSurfaceMesh();

  void CreateRenderData(VkRenderPass renderPass, const uint32_t kImageCount,
                        const VkExtent2D kFramebufferExtent,
                        const bool kFramebufferHasDepthAttachment);

  void Prepare(VkCommandBuffer cmdBuffer);

  void Update(float dt);

  void PrepareRender(const uint32_t frameIndex, VkCommandBuffer cmdBuffer,
                     const glm::mat4 &viewMat, const glm::mat4 &projMat,
                     const glm::vec3 &camPos,
                     const SkyModel::Params &skyParams);

  void Render(const uint32_t frameIndex, VkCommandBuffer cmdBuffer);

  // @pre Called inside ImGui Window scope
  void ShowGUISettings();

private:
  // TODO batch

  void PrepareModelTess(VkCommandBuffer cmdBuffer);

  void ShowWaterSurfaceSettings();
  void ShowLightingSettings();
  void ShowMeshSettings();

  void UpdateUniformBuffer(const uint32_t imageIndex);
  void UpdateDescriptorSets();
  void UpdateDescriptorSet(const uint32_t frameIndex);
  void SetDescriptorSetsDirty();

  void CreateDescriptorSetLayout();
  void CreateUniformBuffers(const uint32_t kBufferCount);
  void SetupPipeline();
  void CreateDescriptorSets(const uint32_t kCount);

  void CreateTessendorfModel();

  void CreateMesh();
  void CreateGridVertices(const uint32_t kTileSize, const float kScale);
  void CreateGridIndices(const uint32_t kTileSize);

  void CreateStagingBuffer();
  void CreateFrameMaps(VkCommandBuffer cmdBuffer);
  std::unique_ptr<Image> CreateMap(VkCommandBuffer cmdBuffer,
                                   const uint32_t kSize,
                                   const VkFormat kMapFormat,
                                   const bool kUseMipMapping);

  struct FrameMapData;

  void UpdateFrameMaps(VkCommandBuffer cmdBuffer, FrameMapData &frame);
  void CopyModelTessDataToStagingBuffer();

  uint32_t GetTotalVertexCount(const uint32_t kTileSize) const {
    return (kTileSize + 1) * (kTileSize + 1);
  }

  uint32_t GetTotalIndexCount(const uint32_t kTotalVertexCount) const {
    const uint32_t kIndicesPerTriangle = 3, kTrianglesPerQuad = 2;
    return kTotalVertexCount * kIndicesPerTriangle * kTrianglesPerQuad;
  }

  static uint32_t GetMaxVertexCount() {
    return (s_kMaxTileSize + 1) * (s_kMaxTileSize + 1);
  }
  static uint32_t GetMaxIndexCount() {
    const uint32_t kIndicesPerTriangle = 3, kTrianglesPerQuad = 2;
    return GetMaxVertexCount() * kIndicesPerTriangle * kTrianglesPerQuad;
  }

private:
  EngineContext &context;

  std::unique_ptr<DescriptorSetLayout> m_DescriptorSetLayout{nullptr};

  struct DescriptorSet {
    bool isDirty{true};
    VkDescriptorSet set{VK_NULL_HANDLE};
  };
  std::vector<DescriptorSet> m_DescriptorSets;

  std::vector<Buffer> m_UniformBuffers;

  std::unique_ptr<GraphicsPipeline> m_Pipeline{nullptr};

  // =========================================================================
  // Mesh properties
  std::unique_ptr<Mesh<Vertex>> m_Mesh{nullptr};

  uint32_t m_TileSize{WSTessendorf::s_kDefaultTileSize};
  float m_VertexDistance{WSTessendorf::s_kDefaultTileLength /
                         static_cast<float>(WSTessendorf::s_kDefaultTileSize)};
  // Model properties
  std::unique_ptr<WSTessendorf> m_ModelTess{nullptr};

  bool m_PlayAnimation{true};
  float m_TimeCtr{0.0};
  float m_AnimSpeed{3.0};

  // -------------------------------------------------------------------------
  // Water Surface textures
  //  both displacementMap and normalMap are generated on the CPU, then
  //  transferred to the GPU, per frame

  static constexpr VkFormat s_kMapFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
  static constexpr bool s_kUseMipMapping = false;

  std::unique_ptr<Buffer> m_StagingBuffer{nullptr};

  struct FrameMapData {
    std::unique_ptr<Image> displacementMap{nullptr};
    std::unique_ptr<Image> normalMap{nullptr};
  } data;

  struct FrameMapPair {
    std::array<FrameMapData, 1> data;
  };

  // Map pair is preallocated for each size of the model's data
  std::vector<FrameMapPair> m_FrameMaps;
  // Bound pair for the current model's size
  FrameMapPair *m_CurFrameMap{nullptr};

  // Whether the frame maps need to update after resolution has been changed
  bool m_FrameMapNeedsUpdate{false};

  // -------------------------------------------------------------------------
  // Uniform buffers data

  struct VertexUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    float WSHeightAmp;
    float WSChoppy;
    float scale{1.0f}; ///< Texture scale
  };
  VertexUBO m_VertexUBO{};

  struct WaterSurfaceUBO {
    alignas(16) glm::vec3 camPos;
    float height{50.0f};
    alignas(16) glm::vec3 absorpCoef{0.420, 0.063, 0.019};
    alignas(16) glm::vec3 scatterCoef{ComputeScatteringCoefPA01(0.037)};
    alignas(16) glm::vec3 backscatterCoef{
        ComputeBackscatteringCoefPA01(scatterCoef)};
    // -------------------------------------------------
    alignas(16) glm::vec3 terrainColor{0.964, 1.0, 0.824};
    float skyIntensity{1.0};
    float specularIntensity{1.0};
    float specularHighlights{32.0};
    SkyModel::Params sky;
  };
  WaterSurfaceUBO m_WaterSurfaceUBO{};

  // -------------------------------------------------------------------------
  // GUI stuff

  template <typename T, size_t S> struct ValueStringArray {
    const std::array<T, S> types;
    const std::array<const char *, S> strings;
    inline constexpr T operator[](uint32_t i) const { return types[i]; }
    inline constexpr operator auto() const { return strings; }
    inline constexpr size_t size() const { return S; }
    inline const auto begin() const { return cbegin(types); }
    inline constexpr auto find(const T &e) const {
      return std::find(cbegin(types), cend(types), e);
    }
    inline constexpr uint32_t GetIndex(const T &e) const {
      return std::find(cbegin(types), cend(types), e) - begin();
    }
  };

  bool m_ClampHeight{true};

  static const inline glm::vec3 s_kWavelengthsRGB_m{680e-9, 550e-9, 440e-9};
  static const inline glm::vec3 s_kWavelengthsRGB_nm{680, 550, 440};

  // Should correspond to s_kMaxTileSize and s_kMinTileSize range
  static const inline ValueStringArray<uint32_t, 7> s_kWSResolutions{
      {16, 32, 64, 128, 256, 512, 1024},
      {"16", "32", "64", "128", "256", "512", "1024"}};

  // =========================================================================
  // Jerlov water types: a classification based on coefficient K_d(\lambda)

  // [PA01] Premože S., Ashikhmin M. *Rendering Natural Waters*.
  //      Computer Graphics forum. EUROGRAPHICS 2001.
  //(diffuse attenuation coefficient for downwelling irradiance) [PA01]
  // twelve water types with assigned K_d(\lambda) spectrum
  //  I to III are for open ocean waters:
  //      with type I being the clearest, type III being the most turbid
  //  1-9 correspond to coastal waters, from clearest to the most turbid

  // Values of K_d for wavelengths: Red (680 nm), Green (550 nm), Blue (440 nm)
  //  INTERPOLATED from measured data in [PA01]
  // In m^-1
  static const inline ValueStringArray<glm::vec3, 8> s_kWaterTypesCoeffsApprox{
      {glm::vec3{0.448, 0.063, 0.0202}, glm::vec3{0.494, 0.089, 0.0732},
       glm::vec3{0.548, 0.120, 0.145}, glm::vec3{0.538, 0.120, 0.294},
       glm::vec3{0.590, 0.190, 0.450}, glm::vec3{0.680, 0.300, 0.648},
       glm::vec3{0.808, 0.460, 1.014}, glm::vec3{0.956, 0.630, 1.720}},
      {"I: Clearest open ocean", "II: Clear open ocean",
       "III: Turbid open ocean", "1: Clearest coastal waters",
       "3: Clear coastal waters", "5: Semi-clear coastal waters",
       "7: Turbid coastal waters", "9: Most turbid coastal waters"}};

  static const inline glm::vec3 s_kWavelengthsData_m{675e-9, 550e-9, 450e-9};
  static const inline glm::vec3 s_kWavelengthsData_nm{675, 550, 450};

  // Values of K_d for wavelengths: Red (675 nm), Green (550 nm), Blue (450 nm)
  //  real measured data from [PA01]
  // In m^-1
  static const inline ValueStringArray<glm::vec3, 8>
      s_kWaterTypesCoeffsAccurate{
          {glm::vec3{0.420, 0.063, 0.019}, glm::vec3{0.465, 0.089, 0.068},
           glm::vec3{0.520, 0.120, 0.135}, glm::vec3{0.510, 0.120, 0.250},
           glm::vec3{0.560, 0.190, 0.390}, glm::vec3{0.650, 0.300, 0.560},
           glm::vec3{0.780, 0.460, 0.890}, glm::vec3{0.920, 0.630, 1.600}},
          {"I: Clearest open ocean", "II: Clear open ocean",
           "III: Turbid open ocean", "1: Clearest coastal waters",
           "3: Clear coastal waters", "5: Semi-clear coastal waters",
           "7: Turbid coastal waters", "9: Most turbid coastal waters"}};

  // Scattering coefficient at wavelength0 = 514 nm for water types, [PA01]
  //  in m^-1
  static constexpr ValueStringArray<float, 3> s_kScatterCoefLambda0{
      {0.037, 0.219, 1.824},
      {"I: Clear ocean", "1: Coastal ocean", "9: Turbid harbor"}};

  uint32_t m_WaterTypeCoefIndex{0};
  uint32_t m_BaseScatterCoefIndex{0};

  static glm::vec3 ComputeScatteringCoefPA01(float b_lambda0) {
    return b_lambda0 * ((-0.00113f * s_kWavelengthsRGB_nm + 1.62517f) /
                        (-0.00113f * 514.f + 1.62517f));
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
    const glm::vec3 b_w(0.0007, 0.00173, 0.005);

    // Ratio of backscattering and scattering coeffiecients of the pigments
    const glm::vec3 B_b =
        0.002f + 0.02f *
                     (0.5f - 0.25f * ((1.0f / glm::log(10.0f)) * glm::log(C))) *
                     (550.0f / s_kWavelengthsRGB_nm);
    // Scattering coefficient of the pigment
    const float b_p = 0.3f * glm::pow(C, 0.62f);

    return 0.5f * b_w + B_b * b_p;
  }
};

} // namespace vkh
