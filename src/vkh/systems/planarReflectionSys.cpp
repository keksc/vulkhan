#include "planarReflectionSys.hpp"

#include "../buffer.hpp"
#include "../descriptors.hpp"
#include "../engineContext.hpp"
#include "../image.hpp"
#include "../swapChain.hpp"
#include "vulkan/vulkan.hpp"

namespace vkh {

PlanarReflectionSys::PlanarReflectionSys(EngineContext &context,
                                         glm::uvec2 resolution)
    : System(context), resolution{resolution} {
  const uint32_t imageCount = context.vulkan.swapChain->imageCount();
  colorTargets.reserve(imageCount);
  resolvedColorTargets.reserve(imageCount);
  depthTargets.reserve(imageCount);
  everRendered.resize(imageCount, false);
  reflProjViews.resize(imageCount, glm::mat4(1.f));

  for (uint32_t i = 0; i < imageCount; ++i) {
    ImageCreateInfo_empty colorInfo{};
    colorInfo.size = resolution;
    colorInfo.format = context.vulkan.swapChain->getSwapChainImageFormat();
    colorInfo.samples = context.vulkan.msaaSamples;
    // Attachment-only: this image is never sampled directly (an MSAA image
    // isn't a plain sampler2D water.frag could read) - only its resolve
    // target (resolvedColorTargets[i], below) is.
    colorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment;
    colorInfo.layout = vk::ImageLayout::eUndefined;
    colorInfo.name = "planar reflection MSAA color target";
    colorTargets.emplace_back(context, colorInfo);

    // Single-sample resolve target for colorTargets[i] above - this is
    // what actually gets bound to be sampled for reflections.
    ImageCreateInfo_empty resolvedColorInfo{};
    resolvedColorInfo.size = resolution;
    resolvedColorInfo.format =
        context.vulkan.swapChain->getSwapChainImageFormat();
    resolvedColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eSampled;
    resolvedColorInfo.layout = vk::ImageLayout::eUndefined;
    resolvedColorInfo.name = "planar reflection resolved color target";
    resolvedColorTargets.emplace_back(context, resolvedColorInfo);

    ImageCreateInfo_empty depthInfo{};
    depthInfo.size = resolution;
    depthInfo.format = context.vulkan.swapChain->getSwapChainDepthFormat();
    depthInfo.samples = context.vulkan.msaaSamples;
    depthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    depthInfo.aspect = vk::ImageAspectFlagBits::eDepth;
    depthInfo.layout = vk::ImageLayout::eUndefined;
    depthInfo.name = "planar reflection depth target";
    depthTargets.emplace_back(context, depthInfo);
  }

  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler = context.vulkan.device.createSampler(samplerInfo);

  reflectionGlobalUBOs.reserve(imageCount);
  reflectionGlobalSets.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i) {
    auto &buffer = reflectionGlobalUBOs.emplace_back(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible);
    buffer.map();

    vk::DescriptorSet set = context.vulkan.globalDescriptorAllocator->allocate(
        context.vulkan.globalDescriptorSetLayout);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = static_cast<vk::Buffer>(buffer);
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(GlobalUbo);

    DescriptorWriter writer(context);
    writer.writeBuffer(0, bufferInfo, vk::DescriptorType::eUniformBuffer);
    writer.writeBuffer(1, context.vulkan.worldUbo->descriptorInfo(),
                       vk::DescriptorType::eUniformBuffer);
    writer.updateSet(set);

    reflectionGlobalSets.push_back(set);
  }
}

PlanarReflectionSys::~PlanarReflectionSys() {
  if (context.vulkan.device)
    context.vulkan.device.destroySampler(sampler, nullptr);
  // reflectionGlobalSets are NOT individually freed here - matches this
  // codebase's existing pattern (e.g. WaterSys::sets,
  // SkySys::bakedSet) of allocating from globalDescriptorAllocator without
  // an explicit per-set free, presumably because that allocator's own
  // pool gets destroyed/reset as a whole elsewhere. If that assumption is
  // wrong for this engine, these need an explicit free here.
}

glm::mat4 PlanarReflectionSys::computeMirroredView(const glm::mat4 &realView,
                                                   float planeY) const {
  const glm::mat4 invView = glm::inverse(realView);
  const glm::vec3 camPos = glm::vec3(invView[3]);
  const glm::vec3 forward = glm::vec3(-invView[2]);
  const glm::vec3 up = glm::vec3(invView[1]);

  glm::vec3 mirroredCamPos = camPos;
  mirroredCamPos.y = 2.0f * planeY - camPos.y;

  glm::vec3 mirroredForward = forward;
  mirroredForward.y = -forward.y;
  glm::vec3 mirroredUp = up;
  mirroredUp.y = -up.y;

  return glm::lookAt(mirroredCamPos, mirroredCamPos + mirroredForward,
                     mirroredUp);
}

void PlanarReflectionSys::updateReflectionCamera(float planeY) {
  // Read the real camera straight from context - exactly the same fields
  // main.cpp itself reads to fill the REAL GlobalUbo each frame.
  const glm::mat4 &realProj = context.camera.projectionMatrix;
  const glm::mat4 &realView = context.camera.viewMatrix;

  const glm::mat4 mirroredView = computeMirroredView(realView, planeY);
  // Reuses the real camera's projection unchanged - only the view side
  // needs mirroring, and keeping the same FOV/aspect/near/far means the
  // reflection target's framing matches what the real camera would have
  // seen from the mirrored position, which is what makes the reflection
  // line up with the water surface's own on-screen position later in
  // water.tese/water.frag.
  const glm::mat4 projView = realProj * mirroredView;
  reflProjViews[context.frameInfo.frameIndex] = projView;

  GlobalUbo ubo{};
  ubo.proj = realProj;
  ubo.view = mirroredView;
  ubo.projView = projView;
  ubo.inverseView = glm::inverse(mirroredView);
  ubo.resolution = context.window.size;
  ubo.aspectRatio = context.window.aspectRatio;
  ubo.time = context.time;
  applyEnvironment(ubo, context.environment);

  // Clip out anything below the water plane (the lake bed/underlying
  // terrain) from THIS reflection render - see getClipPlane()'s doc
  // comment and entities.vert's PushConstants block. Stored (not just
  // computed inline at the call site in main.cpp) so getClipPlane() stays
  // the single source of truth for "what plane did THIS lake's most
  // recent reflection render actually use" - same reasoning as
  // reflProjViews being stored here rather than recomputed by the caller.
  clipPlane = glm::vec4(0.f, 1.f, 0.f, -planeY);

  auto &buffer = reflectionGlobalUBOs[context.frameInfo.frameIndex];
  buffer.write(&ubo);
  buffer.flush();
}

void PlanarReflectionSys::recordReflectionPass(
    vk::CommandBuffer cmd,
    const std::function<void(vk::CommandBuffer)> &drawOpaqueScene) {
  const int frameIndex = context.frameInfo.frameIndex;
  const bool multisampled =
      context.vulkan.msaaSamples != vk::SampleCountFlagBits::e1;
  Image2D &colorTarget =
      (multisampled ? colorTargets : resolvedColorTargets)[frameIndex];
  Image2D &resolvedColorTarget = resolvedColorTargets[frameIndex];
  Image2D &depthTarget = depthTargets[frameIndex];

  colorTarget.recordTransitionLayout(cmd,
                                     vk::ImageLayout::eColorAttachmentOptimal);
  resolvedColorTarget.recordTransitionLayout(
      cmd, vk::ImageLayout::eColorAttachmentOptimal);
  depthTarget.recordTransitionLayout(
      cmd, vk::ImageLayout::eDepthStencilAttachmentOptimal);

  vk::RenderingAttachmentInfo colorAttachment{};
  colorAttachment.imageView = colorTarget.getImageView();
  colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.storeOp = multisampled ? vk::AttachmentStoreOp::eDontCare
                                         : vk::AttachmentStoreOp::eStore;
  colorAttachment.clearValue.color =
      vk::ClearColorValue{std::array<float, 4>{0.55f, 0.7f, 0.85f, 1.0f}};
  // Sky-tint clear color, matching ssr.comp's own miss-fallback constant
  // (reflColor default there) - a reasonable "nothing drawn here" default
  // if drawOpaqueScene ever leaves part of the target uncovered (e.g. a
  // gap above the horizon it doesn't itself fill).
  //
  // MSAA -> single-sample resolve, same pattern as renderer.cpp's
  // beginMsaaPass() resolving its own MSAA color into the main scene
  // color image: colorTarget itself is storeOp eDontCare (its MSAA
  // samples are never read directly, only the resolved output is), and
  // resolvedColorTarget is what water.frag actually samples afterward.
  colorAttachment.resolveMode = multisampled ? vk::ResolveModeFlagBits::eAverage
                                             : vk::ResolveModeFlagBits::eNone;
  colorAttachment.resolveImageView =
      multisampled ? resolvedColorTarget.getImageView() : vk::ImageView{};
  colorAttachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::RenderingAttachmentInfo depthAttachment{};
  depthAttachment.imageView = depthTarget.getImageView();
  depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue{0.0f, 0};

  vk::RenderingInfo renderingInfo{};
  renderingInfo.renderArea = vk::Rect2D{{0, 0}, {resolution.x, resolution.y}};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = &depthAttachment;

  cmd.beginRendering(renderingInfo);

  vk::Viewport viewport{0.0f,
                        0.0f,
                        static_cast<float>(resolution.x),
                        static_cast<float>(resolution.y),
                        0.0f,
                        1.0f};
  vk::Rect2D scissor{{0, 0}, {resolution.x, resolution.y}};
  cmd.setViewport(0, 1, &viewport);
  cmd.setScissor(0, 1, &scissor);

  drawOpaqueScene(cmd);

  cmd.endRendering();

  // colorTarget (MSAA) never leaves eColorAttachmentOptimal in practice -
  // same reasoning as renderer.cpp's own MSAA color image comment in
  // beginMsaaPass() - only the resolved output actually needs to become
  // sampleable for water.frag.
  resolvedColorTarget.recordTransitionLayout(
      cmd, vk::ImageLayout::eShaderReadOnlyOptimal);

  everRendered[frameIndex] = true;
}

VkDescriptorImageInfo PlanarReflectionSys::getReflectionImageInfo() const {
  auto info =
      resolvedColorTargets[context.frameInfo.frameIndex].getDescriptorInfo(
          sampler);
  return info;
}

} // namespace vkh
