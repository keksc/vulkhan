#include "waterSurfaceMesh.hpp"

#include <fmt/color.h>
#include <fmt/core.h>
#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"
#include "../../renderer.hpp"
#include "../../swapChain.hpp"

namespace vkh {
namespace waterSys {
struct VertexUBO {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
  float WSHeightAmp;
  float WSChoppy;
  float scale{1.0f}; ///< Texture scale
} m_VertexUBO;

static const inline glm::vec3 s_kWavelengthsRGB_nm{680, 550, 440};
static glm::vec3 ComputeBackscatteringCoefPA01(const glm::vec3 &b) {
  return 0.01829f * b + 0.00006f;
}
static glm::vec3 ComputeScatteringCoefPA01(float b_lambda0) {
  return b_lambda0 * ((-0.00113f * s_kWavelengthsRGB_nm + 1.62517f) /
                      (-0.00113f * 514.f + 1.62517f));
}
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
  skySys::Params sky;
} m_WaterSurfaceUBO;
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
std::unique_ptr<DescriptorSetLayout> descriptorSetLayout{nullptr};
uint32_t tileSize{WSTessendorf::s_kDefaultTileSize};
std::unique_ptr<Mesh<Vertex>> mesh;
float vertexDistance{WSTessendorf::s_kDefaultTileLength /
                     static_cast<float>(WSTessendorf::s_kDefaultTileSize)};
// Model properties
std::unique_ptr<WSTessendorf> modelTess;

uint32_t GetTotalVertexCount(const uint32_t kTileSize) {
  return (kTileSize + 1) * (kTileSize + 1);
}

uint32_t GetTotalIndexCount(const uint32_t totalVertexCount) {
  const uint32_t indicesPerTriangle = 3, trianglesPerQuad = 2;
  return totalVertexCount * indicesPerTriangle * trianglesPerQuad;
}

static uint32_t GetMaxVertexCount() {
  return (maxTileSize + 1) * (maxTileSize + 1);
}
static uint32_t GetMaxIndexCount() {
  const uint32_t kIndicesPerTriangle = 3, kTrianglesPerQuad = 2;
  return GetMaxVertexCount() * kIndicesPerTriangle * kTrianglesPerQuad;
}

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

// Should correspond to s_kMaxTileSize and s_kMinTileSize range
static const inline ValueStringArray<uint32_t, 7> s_kWSResolutions{
    {16, 32, 64, 128, 256, 512, 1024},
    {"16", "32", "64", "128", "256", "512", "1024"}};

bool playAnim{true};
float timeCtrl{0.0};
float animSpeed{3.0};

struct FrameMapData {
  std::unique_ptr<Image> displacementMap{nullptr};
  std::unique_ptr<Image> normalMap{nullptr};
} data;
std::vector<FrameMapData> frameMaps;
FrameMapData *currentFrameMap;

bool m_FrameMapNeedsUpdate{false};
static constexpr VkFormat s_kMapFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
static constexpr bool s_kUseMipMapping = false;

std::unique_ptr<Buffer> m_StagingBuffer{nullptr};

struct DescriptorSet {
  bool isDirty{true};
  VkDescriptorSet set{VK_NULL_HANDLE};
};
std::vector<DescriptorSet> descriptorSets;

std::vector<Buffer> uniformBuffers;

std::unique_ptr<GraphicsPipeline> pipeline;
void UpdateDescriptorSet(EngineContext &context, const uint32_t frameIndex) {
  assert(frameIndex < descriptorSets.size());

  auto &set = descriptorSets[frameIndex];
  if (set.isDirty == false)
    return;

  // Add water surface uniform buffers
  VkDescriptorBufferInfo bufferInfos[2] = {};
  bufferInfos[0].buffer = uniformBuffers[frameIndex];
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(VertexUBO);

  bufferInfos[1].buffer = uniformBuffers[frameIndex];
  bufferInfos[1].offset = getUniformBufferAlignment(context, sizeof(VertexUBO));
  bufferInfos[1].range = sizeof(WaterSurfaceUBO);

  const auto &kFrameMaps = currentFrameMap;

  assert(kFrameMaps->displacementMap != nullptr);
  assert(kFrameMaps->normalMap != nullptr);

  VkDescriptorImageInfo imageInfos[2] = {};
  imageInfos[0] = kFrameMaps->displacementMap->getDescriptorInfo();
  // TODO force future image layout
  imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[1] = kFrameMaps->normalMap->getDescriptorInfo();
  // TODO force future image layout
  imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  DescriptorWriter descriptorWriter(*descriptorSetLayout,
                                    *context.vulkan.globalDescriptorPool);

  descriptorWriter.writeBuffer(0, &bufferInfos[0])
      .writeBuffer(1, &bufferInfos[1])
      .writeImage(2, &imageInfos[0])
      .writeImage(3, &imageInfos[1]);

  descriptorWriter.overwrite(set.set);
  set.isDirty = false;
}

/**
 * @pre size > 0
 * @return Size 'size' aligned to 'alignment'
 */
static size_t alignSizeTo(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

// =============================================================================

void createGridVertices(const uint32_t kTileSize, const float kScale) {
  vertices.reserve(GetTotalVertexCount(kTileSize));

  const int32_t kHalfSize = kTileSize / 2;

  for (int32_t y = -kHalfSize; y <= kHalfSize; ++y) {
    for (int32_t x = -kHalfSize; x <= kHalfSize; ++x) {
      vertices.emplace_back(
          // Position
          glm::vec3(static_cast<float>(x), // x
                    0.0f,                  // y
                    static_cast<float>(y)  // z
                    ) *
              kScale,
          // Texcoords
          glm::vec2(static_cast<float>(x + kHalfSize), // u
                    static_cast<float>(y + kHalfSize)  // v
                    ) /
              static_cast<float>(kTileSize));
    }
  }
}

void CreateGridIndices(const uint32_t kTileSize) {
  const uint32_t kVertexCount = kTileSize + 1;

  indices.reserve(GetTotalIndexCount(kVertexCount * kVertexCount));

  for (uint32_t y = 0; y < kTileSize; ++y) {
    for (uint32_t x = 0; x < kTileSize; ++x) {
      const uint32_t kVertexIndex = y * kVertexCount + x;

      // Top triangle
      indices.emplace_back(kVertexIndex);
      indices.emplace_back(kVertexIndex + kVertexCount);
      indices.emplace_back(kVertexIndex + 1);

      // Bottom triangle
      indices.emplace_back(kVertexIndex + 1);
      indices.emplace_back(kVertexIndex + kVertexCount);
      indices.emplace_back(kVertexIndex + kVertexCount + 1);
    }
  }
}
void CreateDescriptorSetLayout(EngineContext &context) {
  uint32_t bindingPoint = 0;

  descriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          // VertexUBO
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      VK_SHADER_STAGE_VERTEX_BIT)
          // WaterSurfaceUBO
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      VK_SHADER_STAGE_FRAGMENT_BIT)
          // Displacement map
          .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT)
          // Normal map
          .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT)
          .build();
}
void SetupPipeline(EngineContext &context) {
  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo.setLayoutCount = 1;
  VkDescriptorSetLayout descSetLayout = *descriptorSetLayout;
  pipelineInfo.layoutInfo.pSetLayouts = &descSetLayout;
  pipelineInfo.attributeDescriptions = Vertex::s_AttribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::s_BindingDescriptions;
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);

  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/waterSurfaceMesh.vert.spv",
      "shaders/waterSurfaceMesh.frag.spv", pipelineInfo);

  assert(descriptorSetLayout != nullptr);
  assert(static_cast<VkDescriptorSetLayout>(*descriptorSetLayout) !=
         VK_NULL_HANDLE);
}
void CreateStagingBuffer(EngineContext &context) {

  const VkDeviceSize kVerticesSize = sizeof(Vertex) * GetMaxVertexCount();
  const VkDeviceSize kIndicesSize = sizeof(uint32_t) * GetMaxIndexCount();

  const VkDeviceSize kMapSize =
      Image::formatSize(s_kMapFormat) * maxTileSize * maxTileSize;

  const VkDeviceSize kTotalSize = alignSizeTo(kVerticesSize + kIndicesSize,
                                              Image::formatSize(s_kMapFormat)) +
                                  (kMapSize * 2) // * m_FrameMaps.size()
      ;

  m_StagingBuffer.reset(new Buffer(context, kTotalSize,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
  // TODO can map to lower size after each resize
  m_StagingBuffer->map();
}
void createTessendorfModel() {
  const auto kSampleCount = modelTess->s_kDefaultTileSize;
  const auto kWaveLength = modelTess->s_kDefaultTileLength;

  modelTess = std::make_unique<WSTessendorf>(kSampleCount, kWaveLength);
}
void createMesh(EngineContext &context) {
  const VkDeviceSize kMaxVerticesSize = sizeof(Vertex) * GetMaxVertexCount();
  const VkDeviceSize kMaxIndicesSize = sizeof(uint32_t) * GetMaxIndexCount();

  MeshCreateInfo<Vertex> meshInfo{.vertices = vertices, .indices = indices};

  mesh = std::make_unique<Mesh<Vertex>>(context, meshInfo);
}
void createUniformBuffers(EngineContext &context, const uint32_t kBufferCount) {
  const VkDeviceSize kBufferSize =
      getUniformBufferAlignment(context, sizeof(VertexUBO)) +
      getUniformBufferAlignment(context, sizeof(WaterSurfaceUBO));

  uniformBuffers.reserve(kBufferCount);

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    auto &buffer = uniformBuffers.emplace_back(
        context, kBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    buffer.map();
  }
}
void createDescriptorSets(EngineContext &context, const uint32_t kCount) {
  descriptorSets.resize(kCount);

  VkResult err;
  for (auto &set : descriptorSets) {
    assert(context.vulkan.globalDescriptorPool->allocateDescriptorSet(
        *descriptorSetLayout, set.set));
  }
}
void CreateRenderData(EngineContext &context, VkRenderPass renderPass,
                      const uint32_t kImageCount,
                      const VkExtent2D kFramebufferExtent,
                      const bool kFramebufferHasDepthAttachment) {

  const bool kImageCountChanged = uniformBuffers.size() != kImageCount;
  if (kImageCountChanged) {

    uniformBuffers.clear();
    createUniformBuffers(context, kImageCount);

    descriptorSets.clear();
    createDescriptorSets(context, kImageCount);
    // WARN: Also must update the descriptors before render
  }
}
void init(EngineContext &context) {
  CreateDescriptorSetLayout(context);
  SetupPipeline(context);

  CreateStagingBuffer(context);

  createTessendorfModel();
  createGridVertices(tileSize, vertexDistance);
  CreateGridIndices(tileSize);
  createMesh(context);

  CreateRenderData(context, context.vulkan.swapChain->getRenderPass(),
                   context.vulkan.swapChain->imageCount(),
                   context.vulkan.swapChain->getSwapChainExtent(), true);
}

std::unique_ptr<Image> CreateMap(EngineContext &context,
                                 VkCommandBuffer cmdBuffer,
                                 const uint32_t kSize,
                                 const VkFormat kMapFormat,
                                 const bool kUseMipMapping) {
  ImageCreateInfo imageInfo{};
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  imageInfo.w = kSize;
  imageInfo.h = kSize;
  imageInfo.format = kMapFormat;
  auto map = std::make_unique<Image>(context, imageInfo);

  return std::move(map);
}
void CreateFrameMaps(EngineContext &context, VkCommandBuffer cmdBuffer) {
  // m_kDevice.QueueWaitIdle(vkp::QFamily::Graphics);

  // Create frame map pair for each water surface resolution
  frameMaps.resize(s_kWSResolutions.size());

  for (uint32_t i = 0; i < s_kWSResolutions.size(); ++i) {
    const uint32_t kSize = s_kWSResolutions[i];

    for (auto &frame : frameMaps) {
      frame.displacementMap = std::move(
          CreateMap(context, cmdBuffer, kSize, s_kMapFormat, s_kUseMipMapping));
      frame.normalMap = std::move(
          CreateMap(context, cmdBuffer, kSize, s_kMapFormat, s_kUseMipMapping));
    }
  }
}
void SetDescriptorSetsDirty() {
  // TODO for_each ?
  for (auto &set : descriptorSets)
    set.isDirty = true;
}
void UpdateFrameMaps(VkCommandBuffer cmdBuffer, FrameMapData &frame) {
  VkDeviceSize stagingBufferOffset = alignSizeTo(
      vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32_t),
      Image::formatSize(s_kMapFormat));

  frame.displacementMap->copyFromBuffer(
      cmdBuffer, *m_StagingBuffer, s_kUseMipMapping,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, stagingBufferOffset);

  const VkDeviceSize kMapSize = Image::formatSize(s_kMapFormat) *
                                frame.displacementMap->w *
                                frame.displacementMap->h;
  stagingBufferOffset += kMapSize;

  frame.normalMap->copyFromBuffer(cmdBuffer, *m_StagingBuffer, s_kUseMipMapping,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  stagingBufferOffset);
}
void CopyModelTessDataToStagingBuffer(EngineContext &context) {
  assert(m_StagingBuffer != nullptr);

  const VkDeviceSize kDisplacementsSize =
      sizeof(WSTessendorf::Displacement) * modelTess->GetDisplacementCount();
  const VkDeviceSize kNormalsSize =
      sizeof(WSTessendorf::Normal) * modelTess->GetNormalCount();

  // StagingBuffer layout:
  //  ----------------------------------------------
  // | Vertices | Indices | Displacements | Normals |
  //  ----------------------------------------------
  // mapped

  // Copy displacements
  {
    m_StagingBuffer->write(modelTess->GetDisplacements().data(),
                           kDisplacementsSize);
  }

  // Copy normals
  {
    m_StagingBuffer->write(modelTess->GetNormals().data(), kNormalsSize,
                           kDisplacementsSize);
  }

  m_StagingBuffer->flush(getNonCoherentAtomSizeAlignment(
      context, alignSizeTo(vertices.size() * sizeof(Vertex) +
                               indices.size() * sizeof(uint32_t),
                           Image::formatSize(s_kMapFormat)) +
                   kDisplacementsSize + kNormalsSize));
}
void PrepareModelTess(EngineContext &context, VkCommandBuffer cmdBuffer) {
  modelTess->Prepare();

  // Do one pass to initialize the maps

  m_VertexUBO.WSHeightAmp = modelTess->ComputeWaves(timeCtrl);
  CopyModelTessDataToStagingBuffer(context);

  UpdateFrameMaps(cmdBuffer, *currentFrameMap);
}
void prepare(EngineContext &context, VkCommandBuffer cmdBuffer) {

  CreateFrameMaps(context, cmdBuffer);
  const uint32_t kIndex = 5;
  currentFrameMap = &frameMaps[kIndex];
  SetDescriptorSetsDirty();

  PrepareModelTess(context, cmdBuffer);
}

void update(EngineContext &context, float dt) {
  if (playAnim || m_FrameMapNeedsUpdate) {
    timeCtrl += dt * animSpeed;

    m_VertexUBO.WSHeightAmp = modelTess->ComputeWaves(timeCtrl);
    CopyModelTessDataToStagingBuffer(context);
  }
}

void UpdateUniformBuffer(EngineContext &context, const uint32_t imageIndex) {
  auto &buffer = uniformBuffers[imageIndex];

  void *dataAddr = buffer.getMappedAddr();

  buffer.copyToMapped(&m_VertexUBO, sizeof(m_VertexUBO));
  buffer.copyToMapped(&m_WaterSurfaceUBO, sizeof(m_WaterSurfaceUBO),
                      static_cast<void *>(static_cast<uint8_t *>(dataAddr) +
                                          getUniformBufferAlignment(
                                              context, sizeof(VertexUBO))));

  buffer.flush();
}

void UpdateDescriptorSets(EngineContext &context) {
  assert(descriptorSets.size() == uniformBuffers.size());

  for (uint32_t i = 0; i < descriptorSets.size(); ++i) {
    UpdateDescriptorSet(context, i);
  }
}
void prepareRender(EngineContext &context, const uint32_t frameIndex,
                   VkCommandBuffer cmdBuffer, const glm::mat4 &viewMat,
                   const glm::mat4 &projMat, const glm::vec3 &camPos,
                   const skySys::Params &skyParams) {
  m_VertexUBO.model = glm::mat4(1.0f);
  m_VertexUBO.view = viewMat;
  m_VertexUBO.proj = projMat;

  // Vulkan uses inverted Y coord in comparison to OpenGL (set by glm lib)
  // -> flip the sign on the scaling factor of the Y axis
  m_VertexUBO.proj[1][1] *= -1;
  m_VertexUBO.WSChoppy = modelTess->GetDisplacementLambda();

  m_WaterSurfaceUBO.camPos = camPos;
  if (m_ClampHeight) {
    m_WaterSurfaceUBO.height =
        glm::max(m_WaterSurfaceUBO.height, glm::abs(modelTess->GetMinHeight()));
  }
  m_WaterSurfaceUBO.sky = skyParams;

  UpdateUniformBuffer(context, frameIndex);
  UpdateDescriptorSet(context, frameIndex);

  const uint32_t kTransferIndex = 0;

  // No need to update the texture with the same data over again
  if (playAnim || m_FrameMapNeedsUpdate) {
    UpdateFrameMaps(cmdBuffer, currentFrameMap[kTransferIndex]);

    m_FrameMapNeedsUpdate = false;
  }
}

void render(EngineContext &context, const uint32_t frameIndex,
            VkCommandBuffer cmdBuffer) {
  pipeline->bind(cmdBuffer);

  const uint32_t kFirstSet = 0, kDescriptorSetCount = 1;
  const uint32_t kDynamicOffsetCount = 0;
  const uint32_t *kDynamicOffsets = nullptr;

  mesh->bind(context, cmdBuffer, *pipeline, {descriptorSets[frameIndex].set});
  mesh->draw(cmdBuffer);
}
void cleanup() {
  mesh = nullptr;
  pipeline = nullptr;
  frameMaps.clear();
  m_StagingBuffer = nullptr;
  uniformBuffers.clear();
  descriptorSetLayout = nullptr;
}
} // namespace waterSys
} // namespace vkh
