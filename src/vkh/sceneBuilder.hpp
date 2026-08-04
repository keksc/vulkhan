#pragma once

#include "scene.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <format>
#include <future>

namespace vkh {

template <typename VertexType>
Scene<VertexType>::Scene(EngineContext &context,
                         const std::filesystem::path &path,
                         bool disableMaterial)
    : context{context}, disableMaterial{disableMaterial} {
  loadModelCPU(path);
}

template <typename VertexType>
Scene<VertexType>::Scene(EngineContext &context,
                         const SceneCreateInfo<VertexType> &createInfo)
    : context{context}, disableMaterial{true} {
  ImageCreateInfo_color imageInfo{};
  imageInfo.size = {1, 1};
  glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // White
  color.r = std::pow(color.r, 1.0f / 2.2f);
  color.g = std::pow(color.g, 1.0f / 2.2f);
  color.b = std::pow(color.b, 1.0f / 2.2f);
  uint8_t r = static_cast<uint8_t>(color.r * 255.0f + 0.5f);
  uint8_t g = static_cast<uint8_t>(color.g * 255.0f + 0.5f);
  uint8_t b = static_cast<uint8_t>(color.b * 255.0f + 0.5f);
  uint8_t a = static_cast<uint8_t>(color.a * 255.0f + 0.5f);
  imageInfo.color = (a << 24) | (b << 16) | (g << 8) | r;
  imageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.usage =
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  imageInfo.format = vk::Format::eR8G8B8A8Unorm;
  std::string name = std::format("{:#x} color image", imageInfo.color);
  imageInfo.name = name.c_str();
  images.emplace_back(context, imageInfo);

  createBuffers(createInfo.vertices, createInfo.indices);
}

template <typename VertexType>
void Scene<VertexType>::loadModelCPU(const std::filesystem::path &path) {
  auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
  if (gltfFile.error() != fastgltf::Error::None) {
    throw std::runtime_error(
        std::format("Failed to load {}: {}", path.string(),
                    fastgltf::getErrorMessage(gltfFile.error())));
  }
  fastgltf::Parser parser;
  auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(),
                               fastgltf::Options::LoadExternalBuffers |
                                   fastgltf::Options::LoadExternalImages);
  if (asset.error() != fastgltf::Error::None) {
    throw std::runtime_error(std::format(
        "Failed to parse GLB: {}", fastgltf::getErrorMessage(asset.error())));
  }
  gltf = std::move(asset.get());

  for (const auto &mesh : gltf.meshes) {
    auto &newMesh = meshes.emplace_back();
    uint32_t start = static_cast<uint32_t>(indices.size());
    for (const auto &primitive : mesh.primitives) {
      auto &newPrimitive = newMesh.primitives.emplace_back();
      newPrimitive.indexOffset = start;
      newPrimitive.materialIndex =
          primitive.materialIndex.value_or(0); // Set material index always
      size_t initial_vtx = vertices.size();

      if (primitive.indicesAccessor.has_value()) {
        fastgltf::Accessor &indexAccessor =
            gltf.accessors[primitive.indicesAccessor.value()];
        indices.reserve(indices.size() + indexAccessor.count);
        if (indexAccessor.componentType ==
            fastgltf::ComponentType::UnsignedInt) {
          fastgltf::iterateAccessor<std::uint32_t>(
              gltf, indexAccessor, [this, initial_vtx](std::uint32_t idx) {
                indices.emplace_back(idx +
                                     static_cast<uint32_t>(initial_vtx));
              });
        } else if (indexAccessor.componentType ==
                   fastgltf::ComponentType::UnsignedShort) {
          fastgltf::iterateAccessor<std::uint16_t>(
              gltf, indexAccessor, [this, initial_vtx](std::uint16_t idx) {
                indices.emplace_back(static_cast<uint32_t>(idx) +
                                     static_cast<uint32_t>(initial_vtx));
              });
        } else if (indexAccessor.componentType ==
                   fastgltf::ComponentType::UnsignedByte) {
          fastgltf::iterateAccessor<std::uint8_t>(
              gltf, indexAccessor, [this, initial_vtx](std::uint8_t idx) {
                indices.emplace_back(static_cast<uint32_t>(idx) +
                                     static_cast<uint32_t>(initial_vtx));
              });
        } else {
          throw std::runtime_error("Unsupported index component type");
        }
        uint32_t end = static_cast<uint32_t>(indices.size());
        newPrimitive.indexCount = end - start;
        indexCount += indexAccessor.count;
      }

      auto posAttribute = primitive.findAttribute("POSITION");
      if (posAttribute != primitive.attributes.end()) {
        fastgltf::Accessor &posAccessor =
            gltf.accessors[posAttribute->accessorIndex];
        size_t count = posAccessor.count;

        vertices.resize(vertices.size() + count);
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            gltf, posAccessor,
            [this, initial_vtx, &newMesh](glm::vec3 v, size_t index) {
              vertices[initial_vtx + index].pos = v;
              newMesh.aabb.min = glm::min(newMesh.aabb.min, v);
              newMesh.aabb.max = glm::max(newMesh.aabb.max, v);
            });
      }

      if constexpr (has_normal<VertexType>::value) {
        auto normalAttribute = primitive.findAttribute("NORMAL");
        if (normalAttribute != primitive.attributes.end()) {
          fastgltf::Accessor &normalAccessor =
              gltf.accessors[normalAttribute->accessorIndex];
          fastgltf::iterateAccessorWithIndex<glm::vec3>(
              gltf, normalAccessor,
              [this, initial_vtx](glm::vec3 v, size_t index) {
                vertices[initial_vtx + index].normal = v;
              });
        }
      }

      if constexpr (has_uv<VertexType>::value) {
        auto uvAttribute = primitive.findAttribute("TEXCOORD_0");
        if (uvAttribute != primitive.attributes.end()) {
          fastgltf::Accessor &uvAccessor =
              gltf.accessors[uvAttribute->accessorIndex];
          fastgltf::iterateAccessorWithIndex<glm::vec2>(
              gltf, uvAccessor,
              [this, initial_vtx](glm::vec2 v, size_t index) {
                vertices[initial_vtx + index].uv = v;
              });
        }
      }
      if constexpr (has_skinning<VertexType>::value) {
        auto jointIndicesAttribute = primitive.findAttribute("JOINTS_0");
        if (jointIndicesAttribute != primitive.attributes.end()) {
          fastgltf::Accessor &jointIndicesAccessor =
              gltf.accessors[jointIndicesAttribute->accessorIndex];
          fastgltf::iterateAccessorWithIndex<glm::uvec4>(
              gltf, jointIndicesAccessor,
              [this, initial_vtx](glm::uvec4 v, size_t index) {
                vertices[initial_vtx + index].jointIndices = v;
              });
        }
        auto jointWeightsAttribute = primitive.findAttribute("WEIGHTS_0");
        if (jointWeightsAttribute != primitive.attributes.end()) {
          fastgltf::Accessor &jointWeightsAccessor =
              gltf.accessors[jointWeightsAttribute->accessorIndex];
          fastgltf::iterateAccessorWithIndex<glm::vec4>(
              gltf, jointWeightsAccessor,
              [this, initial_vtx](glm::vec4 v, size_t index) {
                vertices[initial_vtx + index].jointWeights = v;
              });
        }
      }
    }
  }

  nodes.resize(gltf.nodes.size());
  for (size_t i = 0; i < gltf.nodes.size(); ++i) {
    auto &gltfNode = gltf.nodes[i];
    nodes[i].meshIndex = gltfNode.meshIndex;
    if (gltfNode.meshIndex.has_value()) {
      meshes[gltfNode.meshIndex.value()].skinIndex = gltfNode.skinIndex;
    }

    std::visit(
        fastgltf::visitor{[&](fastgltf::math::fmat4x4 matrix) {
                            nodes[i].matrix = glm::make_mat4(matrix.data());
                            nodes[i].useMatrix = true;
                          },
                          [&](fastgltf::TRS trs) {
                            nodes[i].translation =
                                glm::make_vec3(trs.translation.data());
                            nodes[i].rotation =
                                glm::quat(trs.rotation[3], trs.rotation[0],
                                          trs.rotation[1], trs.rotation[2]);
                            nodes[i].scale = glm::make_vec3(trs.scale.data());
                            nodes[i].useMatrix = false;
                          }},
        gltfNode.transform);

    for (auto child : gltfNode.children) {
      nodes[i].children.push_back(child);
    }
  }

  std::vector<bool> isChild(gltf.nodes.size(), false);
  for (auto &n : nodes) {
    for (auto c : n.children)
      isChild[c] = true;
  }
  for (size_t i = 0; i < isChild.size(); ++i) {
    if (!isChild[i])
      rootNodes.push_back(i);
  }

  for (auto &gltfAnim : gltf.animations) {
    auto &anim = animations.emplace_back();

    for (auto &gltfSampler : gltfAnim.samplers) {
      auto &sampler = anim.samplers.emplace_back();
      sampler.interpolation = gltfSampler.interpolation;

      auto &inputAccessor = gltf.accessors[gltfSampler.inputAccessor];
      sampler.inputs.reserve(inputAccessor.count);
      fastgltf::iterateAccessor<float>(gltf, inputAccessor, [&](float v) {
        sampler.inputs.push_back(v);
        anim.start = std::min(anim.start, v);
        anim.end = std::max(anim.end, v);
      });

      auto &outputAccessor = gltf.accessors[gltfSampler.outputAccessor];
      sampler.outputsVec4.reserve(outputAccessor.count);
      if (outputAccessor.type == fastgltf::AccessorType::Vec3) {
        fastgltf::iterateAccessor<glm::vec3>(
            gltf, outputAccessor, [&](glm::vec3 v) {
              sampler.outputsVec4.emplace_back(v.x, v.y, v.z, 0.0f);
            });
      } else if (outputAccessor.type == fastgltf::AccessorType::Vec4) {
        fastgltf::iterateAccessor<glm::vec4>(
            gltf, outputAccessor,
            [&](glm::vec4 v) { sampler.outputsVec4.push_back(v); });
      }
    }

    for (auto &gltfChannel : gltfAnim.channels) {
      if (!gltfChannel.nodeIndex.has_value())
        continue;
      auto &channel = anim.channels.emplace_back();
      channel.path = gltfChannel.path;
      channel.nodeIndex = gltfChannel.nodeIndex.value();
      channel.samplerIndex = gltfChannel.samplerIndex;
    }

    if (anim.start > anim.end) {
      anim.start = 0.0f;
      anim.end = 0.0f;
    }
  }
  for (const auto &gltfSkin : gltf.skins) {
    auto &skin = skins.emplace_back();
    for (auto joint : gltfSkin.joints) {
      skin.joints.push_back(joint);
    }
    if (gltfSkin.inverseBindMatrices.has_value()) {
      fastgltf::Accessor &ibmAccessor =
          gltf.accessors[gltfSkin.inverseBindMatrices.value()];
      skin.inverseBindMatrices.reserve(ibmAccessor.count);
      fastgltf::iterateAccessor<fastgltf::math::fmat4x4>(
          gltf, ibmAccessor, [&](fastgltf::math::fmat4x4 m) {
            skin.inverseBindMatrices.push_back(glm::make_mat4(m.data()));
          });
    } else {
      skin.inverseBindMatrices.assign(skin.joints.size(), glm::mat4(1.0f));
    }
  }

  for (auto root : rootNodes) {
    updateNodeTransforms(root, glm::mat4(1.0f));
  }
}

template <typename VertexType>
void Scene<VertexType>::uploadModelGPU(vk::DescriptorSetLayout setLayout) {
  if (!disableMaterial) {
    for (auto &image : gltf.images) {
      std::visit(
          fastgltf::visitor{
              [](auto &arg) {},
              [&](fastgltf::sources::BufferView &view) {
                auto &bufferView = gltf.bufferViews[view.bufferViewIndex];
                auto &buffer = gltf.buffers[bufferView.bufferIndex];
                std::visit(
                    fastgltf::visitor{
                        [](auto &arg) {},
                        [&](fastgltf::sources::Array &vector) {
                          ImageCreateInfo_PNGdata createInfo;
                          createInfo.data = reinterpret_cast<void *>(
                              vector.bytes.data() + bufferView.byteOffset);
                          createInfo.dataSize =
                              static_cast<size_t>(bufferView.byteLength);
                          std::string name =
                              std::format("image {} for scene {}", image.name,
                                          createInfo.name);
                          images.emplace_back(context, createInfo);
                        }},
                    buffer.data);
              },
          },
          image.data);
    }
    for (auto &material : gltf.materials) {
      typename Scene<VertexType>::Material mat{};
      mat.baseColorFactor =
          glm::make_vec4(material.pbrData.baseColorFactor.data());
      mat.roughnessFactor = material.pbrData.roughnessFactor;
      auto &baseColorTexture = material.pbrData.baseColorTexture;
      auto &metallicRoughnessTexture =
          material.pbrData.metallicRoughnessTexture;
      if (baseColorTexture.has_value()) {
        auto &tex = gltf.textures[baseColorTexture.value().textureIndex];
        mat.baseColorTextureIndex = tex.imageIndex;
      }
      if (metallicRoughnessTexture.has_value()) {
        auto &tex =
            gltf.textures[metallicRoughnessTexture.value().textureIndex];
        mat.metallicRoughnessTextureIndex = tex.imageIndex;
      }
      auto &normalTexture = material.normalTexture;
      if (normalTexture.has_value()) {
        auto &tex = gltf.textures[normalTexture.value().textureIndex];
        mat.normalTextureIndex = tex.imageIndex;
      }
      materials.emplace_back(mat);
    }
    typename Scene<VertexType>::Material mat{};
    if (materials.empty()) {
      mat.baseColorFactor = glm::vec4{};
      materials.emplace_back(mat);
    }

    sceneTextureSet =
        context.vulkan.globalDescriptorAllocator->allocate(setLayout);

    if (!images.empty()) {
      std::vector<vk::DescriptorImageInfo> imageInfos;
      imageInfos.reserve(images.size());
      for (auto &img : images) {
        // Cast raw VkDescriptorImageInfo from wrapper wrapper structure to
        // vk:: equivalent if required
        imageInfos.push_back(
            img.getDescriptorInfo(context.vulkan.defaultSampler));
      }

      vk::WriteDescriptorSet write{
          sceneTextureSet,                           // dstSet
          0,                                         // dstBinding
          0,                                         // dstArrayElement
          static_cast<uint32_t>(imageInfos.size()),  // descriptorCount
          vk::DescriptorType::eCombinedImageSampler, // descriptorType
          imageInfos.data()                          // pImageInfo
      };

      context.vulkan.device.updateDescriptorSets(1, &write, 0, nullptr);
    }
  }

  createBuffers(vertices, indices);

  vertices.clear();
  vertices.shrink_to_fit();
  indices.clear();
  indices.shrink_to_fit();
}

template <typename VertexType>
void Scene<VertexType>::createBuffers(const std::vector<VertexType> &vertices,
                                      const std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.empty()) {
    throw std::runtime_error(
        "Cannot create buffers with empty vertices or indices");
  }
  indexCount = static_cast<uint32_t>(indices.size());
  vertexCount = static_cast<uint32_t>(vertices.size());

  vk::DeviceSize indicesSize = sizeof(uint32_t) * indexCount;
  vk::DeviceSize verticesSize = sizeof(VertexType) * vertexCount;

  Buffer<std::byte> stagingBuffer(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      verticesSize + indicesSize);

  vk::BufferUsageFlags extraFlags;
  // if (context.vulkan.isRayTracingAvailable())
  //   extraFlags |=
  //       vk::BufferUsageFlagBits::eShaderDeviceAddress |
  //       vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;

  indexBuffer = std::make_unique<Buffer<uint32_t>>(
      context,
      vk::BufferUsageFlagBits::eIndexBuffer |
          vk::BufferUsageFlagBits::eTransferDst | extraFlags,
      vk::MemoryPropertyFlagBits::eDeviceLocal, indexCount);

  vertexBuffer = std::make_unique<Buffer<VertexType>>(
      context,
      vk::BufferUsageFlagBits::eVertexBuffer |
          vk::BufferUsageFlagBits::eTransferDst | extraFlags,
      vk::MemoryPropertyFlagBits::eDeviceLocal, vertexCount);

  stagingBuffer.map();
  stagingBuffer.write(indices.data(), indicesSize);
  stagingBuffer.write(vertices.data(), verticesSize, indicesSize);

  vk::BufferCopy copyRegion{
      0,          // srcOffset
      0,          // dstOffset
      indicesSize // size
  };

  auto cmd = beginSingleTimeCommands(context);
  cmd.copyBuffer(stagingBuffer, *indexBuffer, 1, &copyRegion);

  copyRegion.srcOffset = indicesSize;
  copyRegion.size = verticesSize;
  cmd.copyBuffer(stagingBuffer, *vertexBuffer, 1, &copyRegion);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  if (meshes.empty()) {
    auto &newMesh = meshes.emplace_back();
    auto &newPrimitive = newMesh.primitives.emplace_back();
    newPrimitive.indexOffset = 0;
    newPrimitive.indexCount = indexCount;
    newPrimitive.materialIndex = 0;
  }
}

// Builds a Scene on a background thread (CPU-side glTF parsing), then hands
// it back so the caller can finish it off with the Vulkan upload
// (uploadModelGPU) on the main/graphics thread.
template <typename VertexType> class SceneBuilder {
public:
  SceneBuilder(EngineContext &context, const std::filesystem::path &path,
               bool disableMaterial = false) {
    // Async work to load scene
    futureScene =
        std::async(std::launch::async, [&context, path, disableMaterial]() {
          auto scene =
              std::make_shared<Scene<VertexType>>(context, disableMaterial);
          scene->loadModelCPU(path);
          return scene;
        });
  }

  // Non-blocking check to see if the background thread has finished
  bool isReady() const {
    return futureScene.valid() &&
           futureScene.wait_for(std::chrono::seconds(0)) ==
               std::future_status::ready;
  }

  // Is it's not ready, will block
  // Executes the Vulkan commands and hands over the final Scene.
  std::shared_ptr<Scene<VertexType>> build(vk::DescriptorSetLayout layout) {
    auto scene = futureScene.get();
    scene->uploadModelGPU(layout);
    return scene;
  }

private:
  std::future<std::shared_ptr<Scene<VertexType>>> futureScene;
};

} // namespace vkh
