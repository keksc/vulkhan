#include "waterSurfaceMesh.hpp"

#include <fmt/color.h>
#include <fmt/core.h>
#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"
#include "../../renderer.hpp"

namespace vkh {
/**
 * @pre size > 0
 * @return Size 'size' aligned to 'alignment'
 */
static size_t AlignSizeTo(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

// =============================================================================

void WaterSurfaceMesh::CreateGridVertices(const uint32_t kTileSize,
                                          const float kScale) {
  std::vector<Vertex> vertices;

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

void WaterSurfaceMesh::CreateGridIndices(const uint32_t kTileSize) {
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
WaterSurfaceMesh::WaterSurfaceMesh(EngineContext &context) : context{context} {
  CreateDescriptorSetLayout();
  SetupPipeline();

  CreateStagingBuffer();

  CreateTessendorfModel();
  CreateGridVertices(m_TileSize, m_VertexDistance);
  CreateGridIndices(m_TileSize);
  CreateMesh();
}

WaterSurfaceMesh::~WaterSurfaceMesh() {}

void WaterSurfaceMesh::CreateRenderData(
    VkRenderPass renderPass, const uint32_t kImageCount,
    const VkExtent2D kFramebufferExtent,
    const bool kFramebufferHasDepthAttachment) {

  const bool kImageCountChanged = m_UniformBuffers.size() != kImageCount;
  if (kImageCountChanged) {

    m_UniformBuffers.clear();
    CreateUniformBuffers(kImageCount);

    m_DescriptorSets.clear();
    CreateDescriptorSets(kImageCount);
    // WARN: Also must update the descriptors before render
  }
}

void WaterSurfaceMesh::Prepare(VkCommandBuffer cmdBuffer) {

  CreateFrameMaps(cmdBuffer);
  const uint32_t kIndex = 5;
  m_CurFrameMap = &m_FrameMaps[kIndex];
  SetDescriptorSetsDirty();

  PrepareModelTess(cmdBuffer);
}

void WaterSurfaceMesh::PrepareModelTess(VkCommandBuffer cmdBuffer) {
  m_ModelTess->Prepare();

  // Do one pass to initialize the maps

  m_VertexUBO.WSHeightAmp = m_ModelTess->ComputeWaves(m_TimeCtr);
  CopyModelTessDataToStagingBuffer();

  UpdateFrameMaps(cmdBuffer, m_CurFrameMap->data[0]);
}

void WaterSurfaceMesh::Update(float dt) {
  if (m_PlayAnimation || m_FrameMapNeedsUpdate) {
    m_TimeCtr += dt * m_AnimSpeed;

    m_VertexUBO.WSHeightAmp = m_ModelTess->ComputeWaves(m_TimeCtr);
    CopyModelTessDataToStagingBuffer();
  }
}

void WaterSurfaceMesh::PrepareRender(const uint32_t frameIndex,
                                     VkCommandBuffer cmdBuffer,
                                     const glm::mat4 &viewMat,
                                     const glm::mat4 &projMat,
                                     const glm::vec3 &camPos,
                                     const SkyModel::Params &skyParams) {
  m_VertexUBO.model = glm::mat4(1.0f);
  m_VertexUBO.view = viewMat;
  m_VertexUBO.proj = projMat;

  // Vulkan uses inverted Y coord in comparison to OpenGL (set by glm lib)
  // -> flip the sign on the scaling factor of the Y axis
  m_VertexUBO.proj[1][1] *= -1;
  m_VertexUBO.WSChoppy = m_ModelTess->GetDisplacementLambda();

  m_WaterSurfaceUBO.camPos = camPos;
  if (m_ClampHeight) {
    m_WaterSurfaceUBO.height = glm::max(m_WaterSurfaceUBO.height,
                                        glm::abs(m_ModelTess->GetMinHeight()));
  }
  m_WaterSurfaceUBO.sky = skyParams;

  UpdateUniformBuffer(frameIndex);
  UpdateDescriptorSet(frameIndex);

  const uint32_t kTransferIndex = 0;

  // No need to update the texture with the same data over again
  if (m_PlayAnimation || m_FrameMapNeedsUpdate) {
    UpdateFrameMaps(cmdBuffer, m_CurFrameMap->data[kTransferIndex]);

    m_FrameMapNeedsUpdate = false;
  }
}

void WaterSurfaceMesh::Render(const uint32_t frameIndex,
                              VkCommandBuffer cmdBuffer) {
  m_Pipeline->bind(cmdBuffer);

  const uint32_t kFirstSet = 0, kDescriptorSetCount = 1;
  const uint32_t kDynamicOffsetCount = 0;
  const uint32_t *kDynamicOffsets = nullptr;

  m_Mesh->bind(context, cmdBuffer, *m_Pipeline,
               {m_DescriptorSets[frameIndex].set});
  m_Mesh->draw(cmdBuffer);
}

// --------------------------------------------------------------------------------
// --------------------------------------------------------------------------------
// Update data functions

void WaterSurfaceMesh::UpdateUniformBuffer(const uint32_t imageIndex) {
  auto &buffer = m_UniformBuffers[imageIndex];

  void *dataAddr = buffer.getMappedAddr();

  buffer.copyToMapped(&m_VertexUBO, sizeof(m_VertexUBO));
  buffer.copyToMapped(&m_WaterSurfaceUBO, sizeof(m_WaterSurfaceUBO),
                      static_cast<void *>(static_cast<uint8_t *>(dataAddr) +
                                          getUniformBufferAlignment(
                                              context, sizeof(VertexUBO))));

  buffer.flush();
}

void WaterSurfaceMesh::UpdateDescriptorSets() {
  assert(m_DescriptorSets.size() == m_UniformBuffers.size());

  for (uint32_t i = 0; i < m_DescriptorSets.size(); ++i) {
    UpdateDescriptorSet(i);
  }
}

void WaterSurfaceMesh::UpdateDescriptorSet(const uint32_t frameIndex) {
  assert(frameIndex < m_DescriptorSets.size());

  auto &set = m_DescriptorSets[frameIndex];
  if (set.isDirty == false)
    return;

  // Add water surface uniform buffers
  VkDescriptorBufferInfo bufferInfos[2] = {};
  bufferInfos[0].buffer = m_UniformBuffers[frameIndex];
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(VertexUBO);

  bufferInfos[1].buffer = m_UniformBuffers[frameIndex];
  bufferInfos[1].offset = getUniformBufferAlignment(context, sizeof(VertexUBO));
  bufferInfos[1].range = sizeof(WaterSurfaceUBO);

  const auto &kFrameMaps = m_CurFrameMap->data[0];

  assert(kFrameMaps.displacementMap != nullptr);
  assert(kFrameMaps.normalMap != nullptr);

  VkDescriptorImageInfo imageInfos[2] = {};
  imageInfos[0] = kFrameMaps.displacementMap->getDescriptorInfo();
  // TODO force future image layout
  imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[1] = kFrameMaps.normalMap->getDescriptorInfo();
  // TODO force future image layout
  imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  DescriptorWriter descriptorWriter(*m_DescriptorSetLayout,
                                    *context.vulkan.globalDescriptorPool);

  descriptorWriter.writeBuffer(0, &bufferInfos[0])
      .writeBuffer(1, &bufferInfos[1])
      .writeImage(2, &imageInfos[0])
      .writeImage(3, &imageInfos[1]);

  descriptorWriter.overwrite(set.set);
  set.isDirty = false;
}

void WaterSurfaceMesh::SetDescriptorSetsDirty() {
  // TODO for_each ?
  for (auto &set : m_DescriptorSets)
    set.isDirty = true;
}

// -----------------------------------------------------------------------------
// Creation functions

void WaterSurfaceMesh::CreateDescriptorSetLayout() {
  uint32_t bindingPoint = 0;

  m_DescriptorSetLayout =
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

void WaterSurfaceMesh::CreateUniformBuffers(const uint32_t kBufferCount) {
  const VkDeviceSize kBufferSize =
      getUniformBufferAlignment(context, sizeof(VertexUBO)) +
      getUniformBufferAlignment(context, sizeof(WaterSurfaceUBO));

  m_UniformBuffers.reserve(kBufferCount);

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    auto &buffer = m_UniformBuffers.emplace_back(
        context, kBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    buffer.map();
  }
}

void WaterSurfaceMesh::SetupPipeline() {
  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo.setLayoutCount = 1;
  VkDescriptorSetLayout descSetLayout = *m_DescriptorSetLayout;
  pipelineInfo.layoutInfo.pSetLayouts = &descSetLayout;
  pipelineInfo.attributeDescriptions = Vertex::s_AttribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::s_BindingDescriptions;
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);

  m_Pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/waterSurfaceMesh.vert.spv",
      "shaders/waterSurfaceMesh.frag.spv", pipelineInfo);

  assert(m_DescriptorSetLayout != nullptr);
  assert(static_cast<VkDescriptorSetLayout>(*m_DescriptorSetLayout) !=
         VK_NULL_HANDLE);
}

void WaterSurfaceMesh::CreateDescriptorSets(const uint32_t kCount) {
  m_DescriptorSets.resize(kCount);

  VkResult err;
  for (auto &set : m_DescriptorSets) {
    assert(context.vulkan.globalDescriptorPool->allocateDescriptorSet(
        *m_DescriptorSetLayout, set.set));
  }
}

void WaterSurfaceMesh::CreateTessendorfModel() {
  const auto kSampleCount = m_ModelTess->s_kDefaultTileSize;
  const auto kWaveLength = m_ModelTess->s_kDefaultTileLength;

  m_ModelTess.reset(new WSTessendorf(kSampleCount, kWaveLength));
}

void WaterSurfaceMesh::CreateMesh() {
  const VkDeviceSize kMaxVerticesSize = sizeof(Vertex) * GetMaxVertexCount();
  const VkDeviceSize kMaxIndicesSize = sizeof(uint32_t) * GetMaxIndexCount();

  MeshCreateInfo<Vertex> meshInfo{.vertices = vertices, .indices = indices};

  m_Mesh.reset(new Mesh<Vertex>(context, meshInfo));
}

void WaterSurfaceMesh::CreateStagingBuffer() {

  const VkDeviceSize kVerticesSize = sizeof(Vertex) * GetMaxVertexCount();
  const VkDeviceSize kIndicesSize = sizeof(uint32_t) * GetMaxIndexCount();

  const VkDeviceSize kMapSize =
      Image::formatSize(s_kMapFormat) * s_kMaxTileSize * s_kMaxTileSize;

  const VkDeviceSize kTotalSize = AlignSizeTo(kVerticesSize + kIndicesSize,
                                              Image::formatSize(s_kMapFormat)) +
                                  (kMapSize * 2) // * m_FrameMaps.size()
      ;

  m_StagingBuffer.reset(new Buffer(context, kTotalSize,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
  // TODO can map to lower size after each resize
  m_StagingBuffer->map();
}

void WaterSurfaceMesh::CreateFrameMaps(VkCommandBuffer cmdBuffer) {
  // m_kDevice.QueueWaitIdle(vkp::QFamily::Graphics);

  // Create frame map pair for each water surface resolution
  m_FrameMaps.resize(s_kWSResolutions.size());

  for (uint32_t i = 0; i < s_kWSResolutions.size(); ++i) {
    const uint32_t kSize = s_kWSResolutions[i];

    for (auto &frame : m_FrameMaps[i].data) {
      frame.displacementMap = std::move(
          CreateMap(cmdBuffer, kSize, s_kMapFormat, s_kUseMipMapping));
      frame.normalMap = std::move(
          CreateMap(cmdBuffer, kSize, s_kMapFormat, s_kUseMipMapping));
    }
  }
}

std::unique_ptr<Image> WaterSurfaceMesh::CreateMap(VkCommandBuffer cmdBuffer,
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

void WaterSurfaceMesh::UpdateFrameMaps(VkCommandBuffer cmdBuffer,
                                       FrameMapData &frame) {
  VkDeviceSize stagingBufferOffset = AlignSizeTo(
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

void WaterSurfaceMesh::CopyModelTessDataToStagingBuffer() {
  assert(m_StagingBuffer != nullptr);

  const VkDeviceSize kDisplacementsSize =
      sizeof(WSTessendorf::Displacement) * m_ModelTess->GetDisplacementCount();
  const VkDeviceSize kNormalsSize =
      sizeof(WSTessendorf::Normal) * m_ModelTess->GetNormalCount();

  // StagingBuffer layout:
  //  ----------------------------------------------
  // | Vertices | Indices | Displacements | Normals |
  //  ----------------------------------------------
  // mapped

  // Copy displacements
  {
    m_StagingBuffer->write(m_ModelTess->GetDisplacements().data(),
                           kDisplacementsSize);
  }

  // Copy normals
  {
    m_StagingBuffer->write(m_ModelTess->GetNormals().data(), kNormalsSize,
                           kDisplacementsSize);
  }

  m_StagingBuffer->flush(getNonCoherentAtomSizeAlignment(
      context, AlignSizeTo(vertices.size() * sizeof(Vertex) +
                               indices.size() * sizeof(uint32_t),
                           Image::formatSize(s_kMapFormat)) +
                   kDisplacementsSize + kNormalsSize));
}
} // namespace vkh
