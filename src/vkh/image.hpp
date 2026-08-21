#pragma once

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <ktx.h>
#include <vk_mem_alloc.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace vkh {

class EngineContext;
constexpr char defaultName[] = "Unnamed image";

struct ImageCreateInfo {
  vk::Format format = vk::Format::eR8G8B8A8Srgb;
  vk::ImageUsageFlags usage =
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  vk::ImageLayout layout = vk::ImageLayout::eUndefined;
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
  uint32_t mipLevels = 1;
  const char *name = defaultName;
};

struct ImageCreateInfo_empty : public ImageCreateInfo {
  glm::uvec2 size{};
};

struct ImageCreateInfo_empty3D : public ImageCreateInfo {
  glm::uvec3 size{};
};

struct ImageCreateInfo_PNGdata : public ImageCreateInfo {
  void *data = nullptr;
  size_t dataSize;
};

struct ImageCreateInfo_data : public ImageCreateInfo {
  void *data = nullptr;
  glm::uvec2 size{};
};

struct ImageCreateInfo_color : public ImageCreateInfo {
  uint32_t color;
  glm::uvec2 size{};
};

struct ImageCreateInfo_cubemapStorage : public ImageCreateInfo {
  uint32_t faceSize{};
};

// Abstract base. Owns the vk::Image/vk::ImageView/allocation and every
// operation that doesn't care about dimensionality (transitions, buffer
// transfers, descriptor info). Subclasses only need to supply their extent
// (and layer count, for Cubemap) plus their own construction logic.
class Image {
public:
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;
  Image &operator=(Image &&) = delete;
  virtual ~Image();

  operator vk::Image() { return img; }
  inline vk::ImageView getImageView() { return view; }
  inline vk::ImageView getView() const { return view; }

  void copyFromBuffer(vk::Buffer buffer, uint32_t bufferOffset = 0);
  void recordCopyFromBuffer(vk::CommandBuffer cmd, vk::Buffer buffer,
                            uint32_t bufferOffset = 0,
                            uint32_t baseArrayLayer = 0,
                            uint32_t layerCount = 1);

  void transitionLayout(vk::ImageLayout newLayout);
  void recordTransitionLayout(vk::CommandBuffer cmd, vk::ImageLayout newLayout);
  void recordTransitionLayout(vk::CommandBuffer cmd, vk::ImageLayout newLayout,
                              vk::ImageSubresourceRange subresourceRange);

  void downloadPixels(unsigned char *dst, uint32_t mipLevel,
                      uint32_t baseArrayLayer = 0, uint32_t layerCount = 1);

  static uint32_t getFormatSize(vk::Format format);

  vk::DescriptorImageInfo getDescriptorInfo(vk::Sampler sampler) const {
    return vk::DescriptorImageInfo(sampler, view, layout);
  }

  // Loads a file (ktx2, png, etc). Returns an Image2D, unless the ktx2 file
  // turns out to contain 6 faces, in which case it returns a Cubemap - the
  // file's contents aren't known until it's parsed, so this has to be a
  // factory rather than living on a single subclass's constructor. Prefer
  // Image2D's or Cubemap's own path constructor when you already know
  // which one you're loading.
  static std::unique_ptr<Image> fromFile(EngineContext &context,
                                         const std::filesystem::path &path);

  unsigned int mipLevels = 1;

protected:
  explicit Image(EngineContext &context) : context{context} {}
  Image(Image &&other) noexcept;

  // Shared by Image2D's and Cubemap's ktx2 path constructors (and fromFile)
  // so the parsing/transcoding logic exists exactly once. `numFaces` is 6
  // for a cubemap, 1 otherwise - callers check it to reject the wrong type
  // (e.g. Image2D's path constructor throwing if handed a cube file).
  struct KtxLoadResult {
    vk::Format format;
    glm::uvec2 size;
    uint32_t numFaces;
    uint32_t numLevels;
    std::vector<vk::BufferImageCopy> regions;
    std::vector<unsigned char> uploadData; // regions index into this
  };
  static KtxLoadResult loadKtx2(EngineContext &context,
                                const std::filesystem::path &path);

  // Creates a vk::Image sized/formatted for `data`, uploads `data.uploadData`
  // via a staging buffer using `data.regions`, and transitions it to
  // eShaderReadOnlyOptimal. Does NOT create any vk::ImageView - 2D and cube
  // need different view types/counts, so that part stays with the caller.
  static std::pair<vk::Image, VmaAllocation>
  createAndUploadKtxImage(EngineContext &context, const KtxLoadResult &data,
                          uint32_t arrayLayers, vk::ImageCreateFlags flags);

  // Every subclass reports its own extent/layer count; the base class uses
  // these for transfers and downloads instead of caring about dimensionality
  // itself.
  virtual vk::Extent3D getExtent(uint32_t mipLevel = 0) const = 0;
  virtual uint32_t getLayerCount() const { return 1; }

  void RecordImageBarrier(vk::CommandBuffer cmd,
                          vk::PipelineStageFlags srcStageMask,
                          vk::PipelineStageFlags dstStageMask,
                          vk::AccessFlags srcAccessMask,
                          vk::AccessFlags dstAccessMask,
                          vk::ImageLayout newLayout) const;

  struct TransitionParams {
    vk::AccessFlags srcAccessMask;
    vk::AccessFlags dstAccessMask;
    vk::PipelineStageFlags srcStage;
    vk::PipelineStageFlags dstStage;
  };
  TransitionParams getTransitionParams(vk::ImageLayout oldLayout,
                                       vk::ImageLayout newLayout);
  void setDbgInfo(const char *name);

  EngineContext &context;

  vk::Image img = nullptr;
  vk::ImageView view = nullptr;
  VmaAllocation allocation = nullptr;
  vk::Format format;
  vk::ImageLayout layout;
  vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1;
  vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
};

// Ordinary 2D image - textures, render targets, 2D storage images. Covers
// everything the old monolithic Image class did except cubemaps and 3D
// volumes.
class Image2D : public Image {
public:
  Image2D(EngineContext &context, const ImageCreateInfo_PNGdata &createInfo);
  Image2D(EngineContext &context, const ImageCreateInfo_data &createInfo);
  Image2D(EngineContext &context, const ImageCreateInfo_color &createInfo);
  Image2D(EngineContext &context, const ImageCreateInfo_empty &createInfo);
  // Loads a .ktx2 or stb-supported (png/jpg/etc) file. Throws if the file
  // is a .ktx2 with 6 faces - that's a Cubemap, use Cubemap's path
  // constructor (or Image::fromFile if you don't know which up front).
  Image2D(EngineContext &context, const std::filesystem::path &path);

  Image2D(Image2D &&other) noexcept : Image(std::move(other)), size{other.size} {}

  glm::uvec2 size{};

  std::vector<unsigned char> downloadAndSerializeToPNG();

protected:
  vk::Extent3D getExtent(uint32_t mipLevel = 0) const override {
    return vk::Extent3D{std::max(1u, size.x >> mipLevel),
                        std::max(1u, size.y >> mipLevel), 1};
  }

private:
  friend class Image; // fromFile() constructs Image2D directly from parsed
                      // ktx data via this private constructor
  Image2D(EngineContext &context) : Image(context) {}

  void createView();
  void createImage(glm::uvec2 size, vk::ImageUsageFlags usage);
  void createImageFromData(void *pixels, size_t dataSize, glm::uvec2 size);
  void loadFromKtx2(const std::filesystem::path &path);
};

// 3D storage/sampled volume - e.g. FluidGrid's dye/velocity/pressure fields.
class Image3D : public Image {
public:
  Image3D(EngineContext &context, const ImageCreateInfo_empty3D &createInfo);

  Image3D(Image3D &&other) noexcept : Image(std::move(other)), size{other.size} {}

  glm::uvec3 size{};

protected:
  vk::Extent3D getExtent(uint32_t mipLevel = 0) const override {
    return vk::Extent3D{std::max(1u, size.x >> mipLevel),
                        std::max(1u, size.y >> mipLevel),
                        std::max(1u, size.z >> mipLevel)};
  }

private:
  void createView();
  void createImage(glm::uvec3 size, vk::ImageUsageFlags usage);
};

// 6-layer cube-compatible image. Exposes two views: getView() (inherited)
// is the eCube sampling view; getArrayView() is the 2DArray view compute
// shaders use for per-face imageStore.
class Cubemap : public Image {
public:
  Cubemap(EngineContext &context, const ImageCreateInfo_cubemapStorage &createInfo);
  // Loads a .ktx2 cubemap (6 faces). Throws if the file isn't a cube - use
  // Image2D's path constructor for plain textures.
  Cubemap(EngineContext &context, const std::filesystem::path &path);

  Cubemap(Cubemap &&other) noexcept
      : Image(std::move(other)), faceSize{other.faceSize},
        arrayView{other.arrayView} {
    other.arrayView = nullptr;
  }
  ~Cubemap() override;

  vk::ImageView getArrayView() const { return arrayView; }

  uint32_t faceSize = 0;

protected:
  vk::Extent3D getExtent(uint32_t mipLevel = 0) const override {
    return vk::Extent3D{std::max(1u, faceSize >> mipLevel),
                        std::max(1u, faceSize >> mipLevel), 1};
  }
  uint32_t getLayerCount() const override { return 6; }

private:
  friend class Image; // fromFile() constructs Cubemap directly from parsed
                      // ktx data via this private constructor
  Cubemap(EngineContext &context) : Image(context) {}

  vk::ImageView arrayView = nullptr;
};

} // namespace vkh
