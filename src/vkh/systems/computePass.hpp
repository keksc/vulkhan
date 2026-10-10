#pragma once

#include <vulkan/vulkan.hpp>

#include <functional>
#include <memory>
#include <vector>

#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"
#include "system.hpp"

namespace vkh {

// Set-1 bindings shared by every postprocessing-style effect: every such
// shader reads the same three inputs/output, so ComputePass owns and wires
// these itself rather than letting each effect redeclare them.
//   binding 0: storage image (output)         - swapchain image, or an
//                                                effect-chain intermediate
//                                                image (see OutputTarget)
//   binding 1: combined image sampler (input) - resolved scene color, or
//                                                the previous stage's
//                                                hand-off image when
//                                                chained (see
//                                                InputTargetProvider)
//   binding 2: combined image sampler (input) - resolved depth
// An effect's own bindings are additional to these and must start at
// kFixedBindingCount.
inline constexpr uint32_t kFixedBindingCount = 3;

// What a ComputePass<PushT> instance writes its main dispatch into.
// Returned by an OutputTargetProvider (see below) - both fields must refer
// to the same underlying image, in vk::ImageLayout::eGeneral.
struct ComputePassOutputTarget {
  vk::Image image;
  vk::ImageView view;
};

// Lets a ComputePass write somewhere other than the real swapchain image -
// e.g. an intermediate handoff image feeding a second, chained
// ComputePass. Indexed the same way descriptorSets/imageIndex are
// everywhere else in this class.
using OutputTargetProvider = std::function<ComputePassOutputTarget(uint32_t)>;

// Lets a ComputePass sample binding 1 (see kFixedBindingCount) from
// somewhere other than the resolved scene color - namely, the previous
// stage's hand-off image, so a chained pass actually builds on the prior
// stage's result instead of every stage independently reprocessing the
// same original scene color. Must return a descriptor already carrying
// the right sampler and vk::ImageLayout::eGeneral (matching how
// intermediate hand-off images are always laid out - see
// PostProcessingSys::makeDescInfos). Indexed the same way
// descriptorSets/imageIndex are everywhere else in this class. Null means
// "read the real resolved scene color" - the default, and always correct
// for whichever pass is first in a chain (or the only pass).
using InputTargetProvider = std::function<vk::DescriptorImageInfo(uint32_t)>;

// Contract a compute shader must satisfy to plug into ComputePass<PushT>.
//
// Only the parts that vary per-effect are exposed here. Everything that
// follows purely from being a postprocessing-style pass - the swapchain
// image / resolved depth / scene color barrier transitions and their
// descriptor bindings (0-2, see kFixedBindingCount), dispatch being
// window-sized, set 0 being the global descriptor set - is fixed by
// ComputePass itself and is not part of this interface.
template <typename PushT> class ComputeEffect {
public:
  virtual ~ComputeEffect() = default;

  // Bindings this effect adds on top of the fixed 0-2 (see
  // kFixedBindingCount) within set 1. Binding indices here must start at
  // kFixedBindingCount and match the shader's layout. Set 0 is the global
  // set and is bound by ComputePass automatically; effects never see it.
  virtual std::vector<vk::DescriptorSetLayoutBinding>
  additionalBindings() const = 0;

  // Optional: per-binding vk::DescriptorBindingFlags, parallel to and
  // same length as additionalBindings() (pad with default-constructed
  // vk::DescriptorBindingFlags{} for bindings that need nothing special).
  // Default empty vector means "no special flags on any additional
  // binding" - ComputePass treats that the same as an all-zero vector.
  // Used by e.g. SmokeCompositeEffect to mark its transmittance-volume
  // array binding VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, so not every
  // slot in a fixed-capacity descriptor array needs a valid written
  // descriptor - see its additionalBindingFlags() override. Requires
  // VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingPartiallyBound
  // (or whichever flags are actually requested) enabled on the device;
  // ComputePass does not check this for you - passing a flag the device
  // doesn't support fails layout creation with a validation error.
  virtual std::vector<vk::DescriptorBindingFlags>
  additionalBindingFlags() const {
    return {};
  }

  virtual const char *shaderPath() const = 0;
  virtual const char *debugName() const = 0;

  // Called once per swapchain image whenever the effect-local descriptor
  // sets are (re)built (initial creation, and on swapchain recreation).
  // The effect must write exactly the bindings it declared in
  // additionalBindings() - bindings 0-2 are written by ComputePass itself.
  // If a binding was marked PARTIALLY_BOUND_BIT via additionalBindingFlags(),
  // the effect may write fewer than its full descriptorCount worth of
  // entries for that binding (see DescriptorWriter::writeImageArray).
  virtual void writeAdditionalDescriptors(DescriptorWriter &writer,
                                          uint32_t imageIndex) = 0;

  // Called once per frame, right before dispatch.
  virtual PushT makePushConstants() const = 0;

  // Optional per-frame precompute hook. Called once per frame from
  // ComputePass::run(), on the same command buffer and for the same
  // swapchain imageIndex ComputePass will use for its own dispatch, right
  // after input transitions and before the main dispatch. Default no-op
  // - only effects that need to write into their own auxiliary resources
  // before the main shader reads them (e.g. a lighting precompute volume)
  // override this. imageIndex is passed through so such an effect can key
  // its own per-image resources the same way ComputePass keys
  // descriptorSets - that's what makes it safe against multiple frames in
  // flight without any extra synchronization: a given imageIndex's
  // resources aren't reused until the presentation engine confirms that
  // index's prior GPU work is done. Effects using this are responsible for
  // any barrier needed between their write and the main dispatch's read.
  virtual void beforeDispatch(vk::CommandBuffer /*cmd*/,
                              uint32_t /*imageIndex*/) {}

  // Optional hook: called whenever the swapchain is (re)created, with the
  // new image count, before descriptor sets are rebuilt. Effects that key
  // per-image resources off imageIndex (see beforeDispatch) override this
  // to resize those resources first - by the time
  // writeAdditionalDescriptors() runs afterward, imageIndex may go up to
  // the new count.
  virtual void onImageCountChanged(uint32_t /*imageCount*/) {}
};

// Type-erased, non-template interface every ComputePass<PushT> satisfies.
// Lets code that chains several passes together (see PostProcessingSys)
// hold a heterogeneous std::vector<std::unique_ptr<IComputePass>> without
// ever needing to name any individual pass's PushT or its
// ComputeEffect<PushT> subclass - only ComputePass<PushT> itself (and
// whoever constructs one) needs to know those.
class IComputePass {
public:
  virtual ~IComputePass() = default;

  virtual void setEnabled(bool value) = 0;
  virtual bool isEnabled() const = 0;

  // Run this pass's dispatch (or its passthrough fallback, if disabled -
  // see runPassthrough()) for the given swapchain image index.
  virtual void run(uint32_t imageIndex) = 0;

  // Bypass this pass entirely: copy the resolved scene color straight
  // into the real swapchain image. Only meaningful on whichever pass owns
  // the real swapchain image (i.e. the last pass in a chain, or the only
  // pass) - see ComputePass::runPassthrough's own doc comment.
  virtual void runPassthrough(uint32_t imageIndex) = 0;

  virtual void recreateDescriptors() = 0;
};

// Generic compute-to-swapchain pass. Owns the pipeline, the effect-local
// descriptor sets, the fixed barrier transitions shared by every
// postprocessing-style effect, the passthrough fallback used while
// disabled, and the window-sized dispatch. Everything shader-specific is
// delegated to the ComputeEffect<PushT> it's constructed with.
//
// Chaining: pass a non-null `outputTarget` to make this instance write
// into an intermediate image instead of the real swapchain image (binding
// 0, and the barrier that would otherwise transition to
// ePresentSrcKHR instead lands the image back in eGeneral so a
// second, chained ComputePass can sample it as an input). Leave it null
// for whichever pass is last in the chain - that one still owns the
// present-layout transition exactly as before. Symmetrically, pass a
// non-null `inputTarget` to make this instance read binding 1 from the
// previous stage's hand-off image instead of the real resolved scene
// color - leave it null for whichever pass is first in the chain (or the
// only pass), which should still see the real scene color. Without a
// non-null inputTarget on every stage but the first, a chain still runs
// but each stage independently reprocesses the original scene color
// rather than building on the previous stage's result, and only the last
// stage's output is ever visible. See PostProcessingSys for how a chain
// of these is assembled and run generically via IComputePass above.
template <typename PushT>
class ComputePass : public IComputePass, public System {
public:
  // `skipInputTransitions`: set true for every pass in a chain EXCEPT the
  // first. Depth and scene color are only ever written once per frame (by
  // the graphics passes before any ComputePass runs) - the FIRST
  // ComputePass to touch them transitions
  // eDepthStencilAttachmentOptimal/eColorAttachmentOptimal to their
  // shader-read-only layouts, and they simply STAY there for the rest of
  // the frame. A second chained pass re-issuing that same "attachment ->
  // read-only" barrier is wrong, not just redundant - Vulkan tracks the
  // image's actual current layout, and asserting oldLayout ==
  // eDepthStencilAttachmentOptimal when it's actually already
  // eDepthStencilReadOnlyOptimal (left there by the first pass) is a
  // validation error (VUID-VkImageMemoryBarrier-oldLayout-01197), not a
  // performance-only mistake. A read-only image being read by a second
  // shader needs no barrier at all (read-after-read is not a hazard), so
  // this flag simply skips barriers[1]/barriers[2] entirely rather than
  // trying to describe some other transition for them.
  ComputePass(EngineContext &context,
              std::unique_ptr<ComputeEffect<PushT>> effect,
              OutputTargetProvider outputTarget = nullptr,
              InputTargetProvider inputTarget = nullptr,
              bool skipInputTransitions = false)
      : System(context), effect{std::move(effect)},
        outputTarget{std::move(outputTarget)},
        inputTarget{std::move(inputTarget)},
        skipInputTransitions{skipInputTransitions} {
    createDescriptors();
    createPipeline();
    savedSwapChain = context.vulkan.swapChain.get();
  }

  ~ComputePass() {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout);
  }

  void setEnabled(bool value) override { enabled = value; }
  bool isEnabled() const override { return enabled; }

  // Non-owning access to the effect, e.g. for calling effect-specific
  // setters that don't belong on this generic interface.
  ComputeEffect<PushT> &getEffect() { return *effect; }

  void recreateDescriptors() override {
    // Give the effect a chance to resize its own per-imageIndex resources
    // (see onImageCountChanged's doc comment) before
    // allocateAndWriteDescriptorSets() below calls writeAdditionalDescriptors()
    // for each new imageIndex - otherwise an effect indexing into a vector
    // sized for the old image count would read out of bounds.
    effect->onImageCountChanged(
        static_cast<uint32_t>(context.vulkan.swapChain->imageCount()));
    descriptorSets.clear();
    allocateAndWriteDescriptorSets();
  }

  void run(uint32_t imageIndex) override {
    if (context.vulkan.swapChain.get() != savedSwapChain) {
      savedSwapChain = context.vulkan.swapChain.get();
      recreateDescriptors();
    }

    if (!enabled) {
      runPassthrough(imageIndex);
      return;
    }

    auto &cmd = context.frameInfo.cmd;

    // Resolve the output target once up front: either the real swapchain
    // image (default) or this instance's chained intermediate image.
    vk::Image outputImage = context.vulkan.swapChain->getImage(imageIndex);
    if (outputTarget) {
      auto target = outputTarget(imageIndex);
      outputImage = target.image;
    }

    vk::ImageMemoryBarrier barriers[3]{};

    // Output image: UNDEFINED -> GENERAL. Using UNDEFINED (rather than
    // preserving prior contents) is fine for both cases - the real
    // swapchain image is about to be presented fresh, and the
    // intermediate handoff image is fully rewritten by this dispatch
    // every frame.
    barriers[0].oldLayout = vk::ImageLayout::eUndefined;
    barriers[0].newLayout = vk::ImageLayout::eGeneral;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = outputImage;
    barriers[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = vk::AccessFlags{};
    barriers[0].dstAccessMask =
        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    uint32_t inputBarrierCount = 0;
    if (!skipInputTransitions) {
      // Depth image: DEPTH_STENCIL_ATTACHMENT_OPTIMAL ->
      // DEPTH_STENCIL_READ_ONLY_OPTIMAL
      barriers[1].oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
      barriers[1].newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
      barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[1].image =
          context.vulkan.swapChain->getResolvedDepthImage(imageIndex);
      barriers[1].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
      barriers[1].subresourceRange.baseMipLevel = 0;
      barriers[1].subresourceRange.levelCount = 1;
      barriers[1].subresourceRange.baseArrayLayer = 0;
      barriers[1].subresourceRange.layerCount = 1;
      barriers[1].srcAccessMask =
          vk::AccessFlagBits::eDepthStencilAttachmentWrite;
      barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

      // Scene color: COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
      // (input)
      barriers[2].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
      barriers[2].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[2].image =
          context.vulkan.swapChain->getSceneColorImage(imageIndex);
      barriers[2].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      barriers[2].subresourceRange.baseMipLevel = 0;
      barriers[2].subresourceRange.levelCount = 1;
      barriers[2].subresourceRange.baseArrayLayer = 0;
      barriers[2].subresourceRange.layerCount = 1;
      barriers[2].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
      barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;

      inputBarrierCount = 2;
    }
    // else: a prior pass in this chain already transitioned depth/scene
    // color to their shader-read-only layouts this frame (see
    // skipInputTransitions' doc comment on the constructor) - both stay
    // there, and a second shader reading them needs no barrier at all
    // (read-after-read is not a hazard).

    // barriers[0] (the output image) always needs its own transition, so
    // this call always runs even when inputBarrierCount is 0 - just with
    // barriers[1..2] included only when relevant.
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe |
                            vk::PipelineStageFlagBits::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits::eLateFragmentTests |
                            vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{}, 0, nullptr, 0, nullptr,
                        1 + inputBarrierCount, barriers);

    // Auxiliary dispatches may sample scene inputs too. Transition them
    // before the hook, including when this is the first chain stage.
    effect->beforeDispatch(cmd, imageIndex);

    pipeline->bind(cmd);

    std::vector<vk::DescriptorSet> sets = {
        context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
        descriptorSets[imageIndex]};
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, pipeline->getLayout(), 0,
        static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    PushT push = effect->makePushConstants();
    cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(PushT), &push);

    uint32_t groupCountX = (context.window.size.x + 15) / 16;
    uint32_t groupCountY = (context.window.size.y + 15) / 16;
    cmd.dispatch(groupCountX, groupCountY, 1);

    if (outputTarget) {
      // Intermediate handoff image: stays GENERAL, just flip write ->
      // read so the next chained ComputePass's dispatch (same command
      // buffer, immediately following) can safely sample it.
      barriers[0].oldLayout = vk::ImageLayout::eGeneral;
      barriers[0].newLayout = vk::ImageLayout::eGeneral;
      barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
      barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;

      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eComputeShader,
                          vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                          &barriers[0]);
    } else {
      // Last pass in the chain (or the only one): hand the real swapchain
      // image off to the presentation engine.
      barriers[0].oldLayout = vk::ImageLayout::eGeneral;
      barriers[0].newLayout = vk::ImageLayout::ePresentSrcKHR;
      barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
      barriers[0].dstAccessMask = vk::AccessFlagBits::eMemoryRead;

      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eBottomOfPipe,
                          vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                          &barriers[0]);
    }
  }

private:
  void createDescriptors() {
    std::vector<vk::DescriptorSetLayoutBinding> allBindings = {
        // Binding 0: Storage Image (Output) — swapchain image, or a
        // chained intermediate image (see outputTarget)
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageImage, 1,
                                       vk::ShaderStageFlagBits::eCompute,
                                       nullptr},
        // Binding 1: Scene Color Sampler (Input) — resolved 1x color
        vk::DescriptorSetLayoutBinding{
            1, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eCompute, nullptr},
        // Binding 2: Depth Sampler (Input)
        vk::DescriptorSetLayoutBinding{
            2, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eCompute, nullptr}};

    auto extra = effect->additionalBindings();
    allBindings.insert(allBindings.end(), extra.begin(), extra.end());

    // Binding-flags path only kicks in if the effect actually asked for
    // any (see ComputeEffect::additionalBindingFlags) - e.g.
    // SmokeCompositeEffect marking its transmittance-volume array
    // PARTIALLY_BOUND_BIT so not every array slot needs a written
    // descriptor. kFixedBindingCount's own 0-2 never get flags - only an
    // effect's own bindings can opt in.
    auto extraFlags = effect->additionalBindingFlags();
    bool anyFlags = false;
    for (auto f : extraFlags)
      if (f)
        anyFlags = true;

    if (!anyFlags) {
      setLayout = buildDescriptorSetLayout(context, allBindings);
    } else {
      // extraFlags must be either empty or exactly extra.size() long -
      // pad with defaults so every additional binding has a
      // corresponding flags entry.
      extraFlags.resize(extra.size());

      std::vector<vk::DescriptorBindingFlags> allFlags(
          kFixedBindingCount, vk::DescriptorBindingFlags{});
      allFlags.insert(allFlags.end(), extraFlags.begin(), extraFlags.end());

      vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
      flagsInfo.bindingCount = static_cast<uint32_t>(allFlags.size());
      flagsInfo.pBindingFlags = allFlags.data();

      // buildDescriptorSetLayout already forwards flags/pNext straight
      // into vk::DescriptorSetLayoutCreateInfo - no need to build that
      // struct by hand here. Requires
      // VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingPartiallyBound
      // (or whichever binding flags are actually requested) enabled on
      // the device; this throws via vk::Device::createDescriptorSetLayout's
      // normal exception-on-failure path (buildDescriptorSetLayout uses
      // the throwing overload, not the vk::Result-returning one) if it
      // isn't.
      setLayout = buildDescriptorSetLayout(context, allBindings,
                                           vk::DescriptorSetLayoutCreateFlags{},
                                           &flagsInfo);
    }

    debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                      reinterpret_cast<uint64_t>(
                          static_cast<VkDescriptorSetLayout>(setLayout)),
                      effect->debugName());

    allocateAndWriteDescriptorSets();
  }

  // Shared by createDescriptors() and recreateDescriptors() so the binding
  // layout only has to be kept in sync with the effect's bindings() in one
  // place.
  void allocateAndWriteDescriptorSets() {
    auto imageCount = context.vulkan.swapChain->imageCount();
    descriptorSets.reserve(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
      auto &set = descriptorSets.emplace_back();
      set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

      vk::DescriptorImageInfo swapImageInfo{};
      if (outputTarget) {
        swapImageInfo.imageView = outputTarget(static_cast<uint32_t>(i)).view;
      } else {
        swapImageInfo.imageView =
            context.vulkan.swapChain->getStorageImageView(i);
      }
      swapImageInfo.imageLayout = vk::ImageLayout::eGeneral;

      // Binding 1 normally reads the real resolved scene color, but a
      // chained stage (non-null inputTarget) instead reads the previous
      // stage's hand-off image, so its effect actually builds on the
      // prior stage's result rather than independently reprocessing the
      // original scene - see InputTargetProvider's doc comment.
      vk::DescriptorImageInfo sceneColorInfo{};
      if (inputTarget) {
        sceneColorInfo = inputTarget(static_cast<uint32_t>(i));
      } else {
        sceneColorInfo.sampler = context.vulkan.defaultSampler;
        sceneColorInfo.imageView =
            context.vulkan.swapChain->getSceneColorView(i);
        sceneColorInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      }

      vk::DescriptorImageInfo depthImageInfo{};
      depthImageInfo.sampler = context.vulkan.defaultSampler;
      depthImageInfo.imageView =
          context.vulkan.swapChain->getResolvedDepthImageView(i);
      depthImageInfo.imageLayout =
          vk::ImageLayout::eDepthStencilReadOnlyOptimal;

      DescriptorWriter writer(context);
      writer.writeImage(0, swapImageInfo, vk::DescriptorType::eStorageImage);
      writer.writeImage(1, sceneColorInfo,
                        vk::DescriptorType::eCombinedImageSampler);
      writer.writeImage(2, depthImageInfo,
                        vk::DescriptorType::eCombinedImageSampler);
      effect->writeAdditionalDescriptors(writer, static_cast<uint32_t>(i));
      writer.updateSet(set);
    }
  }

  void createPipeline() {
    std::vector<vk::DescriptorSetLayout> setLayouts{
        context.vulkan.globalDescriptorSetLayout, setLayout};

    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushT);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    pipeline = std::make_unique<ComputePipeline>(
        context, effect->shaderPath(), layoutInfo, effect->debugName());
  }

public:
  // No compute pass this frame: just copy the resolved scene color directly
  // into the swapchain image and get it into a presentable layout. Scene
  // color and the swapchain image share format and extent (see
  // SwapChain::createSceneColorResources), so a straight copy works - no
  // blit/filtering needed.
  //
  // This ALWAYS targets the real swapchain image, regardless of
  // outputTarget - it's the "postprocessing is off" fallback for a chain
  // as a whole, not a per-pass concept. Only call it on whichever
  // ComputePass instance is last in a chain (or the sole instance, if
  // there's no chaining) - see PostProcessingSys::run().
  //
  // NOTE: any compositing an effect does (e.g. smoke) lives in the compute
  // path above, not here - it will not render at all while the pass is
  // disabled. If that's ever a problem, this is where an effect-specific
  // fallback composite would need to go, which would mean adding a hook
  // to ComputeEffect rather than special-casing it here.
  void runPassthrough(uint32_t imageIndex) override {
    auto &cmd = context.frameInfo.cmd;

    vk::ImageMemoryBarrier barriers[2]{};

    // Scene color: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    barriers[0].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image =
        context.vulkan.swapChain->getSceneColorImage(imageIndex);
    barriers[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;

    // Swapchain image: UNDEFINED -> TRANSFER_DST_OPTIMAL
    barriers[1].oldLayout = vk::ImageLayout::eUndefined;
    barriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = context.vulkan.swapChain->getImage(imageIndex);
    barriers[1].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].srcAccessMask = vk::AccessFlags{};
    barriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 2,
                        barriers);

    auto extent = context.vulkan.swapChain->getSwapChainExtent();

    vk::ImageCopy region{};
    region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.extent = vk::Extent3D{extent.width, extent.height, 1};

    cmd.copyImage(context.vulkan.swapChain->getSceneColorImage(imageIndex),
                  vk::ImageLayout::eTransferSrcOptimal,
                  context.vulkan.swapChain->getImage(imageIndex),
                  vk::ImageLayout::eTransferDstOptimal, 1, &region);

    // Swapchain image: TRANSFER_DST_OPTIMAL -> PRESENT_SRC_KHR
    barriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barriers[1].newLayout = vk::ImageLayout::ePresentSrcKHR;
    barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barriers[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                        &barriers[1]);
  }

private:
  bool enabled{true};

  std::unique_ptr<ComputeEffect<PushT>> effect;
  OutputTargetProvider outputTarget;
  InputTargetProvider inputTarget;
  bool skipInputTransitions;

  std::unique_ptr<ComputePipeline> pipeline;
  vk::DescriptorSetLayout setLayout;
  std::vector<vk::DescriptorSet> descriptorSets;

  SwapChain *savedSwapChain{};
};

} // namespace vkh
