#pragma once

#include "scene.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <cstdio>
#include <format>
#include <future>
#include <print>
#include <string>

namespace vkh {

// Reads the GLB/glTF's extensionsRequired array out of an already-loaded
// fastgltf::GltfDataBuffer (the same one loadModelCPU already read off disk
// via GltfDataBuffer::FromPath) - avoids re-reading a potentially large GLB
// a second time just to report a missing extension. Only used on the parse
// failure path, so any cost here doesn't touch the success path at all.
inline std::vector<std::string>
readExtensionsRequired(fastgltf::GltfDataBuffer &data) {
  data.reset(); // read() advances a cursor - rewind before we start
  std::string contents(data.totalSize(), '\0');
  data.read(reinterpret_cast<std::byte *>(contents.data()), contents.size());

  size_t key = contents.find("\"extensionsRequired\"");
  if (key == std::string::npos)
    return {}; // no required extensions at all

  size_t arrStart = contents.find('[', key);
  size_t arrEnd = contents.find(']', arrStart);
  if (arrStart == std::string::npos || arrEnd == std::string::npos)
    return {};

  std::vector<std::string> result;
  size_t pos = arrStart + 1;
  while (pos < arrEnd) {
    size_t q1 = contents.find('"', pos);
    if (q1 == std::string::npos || q1 > arrEnd)
      break;
    size_t q2 = contents.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 > arrEnd)
      break;
    result.push_back(contents.substr(q1 + 1, q2 - q1 - 1));
    pos = q2 + 1;
  }
  return result;
}

// Groups this asset's meshes into LOD levels by NODE NAME convention: a
// node named "..._LOD0", "..._LOD1", "..._LOD2" etc. (case-sensitive
// "_LOD" immediately followed by a level number) is bucketed into that
// numbered level - 0 being the most detailed, matching this project's
// authoring convention (see fir_sapling_medium's LOD0/1/2 collections,
// each containing several sub-object parts sharing one level). A node
// with no "_LOD<N>" suffix on its name isn't part of any LOD chain and is
// simply not included in the result - the mountain terrain, boid model,
// etc. all fall through this way, since they have no such naming at all.
//
// Unlike MSFT_lod, this needs no raw-JSON workaround: node.name is a
// perfectly ordinary field fastgltf already parses for us.
inline std::vector<std::vector<size_t>>
groupMeshesByLodName(const fastgltf::Asset &gltf) {
  std::vector<std::vector<size_t>> lodLevels;

  for (auto &node : gltf.nodes) {
    if (!node.meshIndex.has_value() || node.name.empty())
      continue;

    std::string name(node.name);
    size_t lodPos = name.rfind("_LOD");
    if (lodPos == std::string::npos)
      continue;

    size_t digitsStart = lodPos + 4; // strlen("_LOD")
    size_t digitsEnd = digitsStart;
    while (digitsEnd < name.size() &&
           std::isdigit(static_cast<unsigned char>(name[digitsEnd])))
      ++digitsEnd;
    if (digitsEnd == digitsStart)
      continue; // "_LOD" wasn't actually followed by a number - not a match

    int level = std::stoi(name.substr(digitsStart, digitsEnd - digitsStart));
    if (level < 0)
      continue;

    if (static_cast<size_t>(level) >= lodLevels.size())
      lodLevels.resize(level + 1);
    lodLevels[level].push_back(node.meshIndex.value());
  }

  return lodLevels;
}

template <typename VertexType>
Scene<VertexType>::Scene(EngineContext &context,
                         const std::filesystem::path &path,
                         bool disableMaterial)
    : context{context}, disableMaterial{disableMaterial} {
  loadModelCPU(path);
}

template <typename VertexType>
Scene<VertexType>::Scene(EngineContext &context,
                         const SceneCreateInfo<VertexType> &createInfo,
                         vk::DescriptorSetLayout setLayout)
    : context{context}, disableMaterial{true} {
  if (!createInfo.texturePixels.empty() && createInfo.textureSize.x > 0 &&
      createInfo.textureSize.y > 0) {
    ImageCreateInfo_data imageInfo{};
    imageInfo.size = createInfo.textureSize;
    imageInfo.data = const_cast<void *>(
        static_cast<const void *>(createInfo.texturePixels.data()));
    imageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageInfo.name = "procedural scene gradient image";
    images.emplace_back(context, imageInfo);
  } else {
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
  }

  // The glTF-loading path is the only one that normally populates
  // `materials`, since it's built from gltf.materials. This constructor has
  // no glTF material data to draw from, but entities.cpp still looks up
  // primitives[0].materialIndex (hardcoded to 0 below in createBuffers())
  // against `materials` to resolve a texture index for the shader; without
  // an entry here it silently falls back to kDefaultMaterial (no texture),
  // so the image we just uploaded above (always exactly one, at index 0)
  // would never actually get sampled.
  Material mat{};
  mat.baseColorFactor = glm::vec4{1.f};
  mat.baseColorTextureIndex = 0;
  materials.emplace_back(mat);

  // The glTF-loading path only allocates/writes sceneTextureSet (set 1) when
  // materials are enabled, since it's built from gltf.materials. This
  // constructor is always disableMaterial=true (no glTF material data to
  // build from). Callers that bind this scene through EntitySys's pipeline
  // (which statically expects set 1) must pass a setLayout so it gets
  // allocated and written here; callers with their own pipeline/descriptor
  // setup (e.g. WaterSys, which manages the displacement texture itself and
  // never binds sceneTextureSet at all) can omit it.
  if (setLayout) {
    sceneTextureSet =
        context.vulkan.globalDescriptorAllocator->allocate(setLayout);

    std::vector<vk::DescriptorImageInfo> imageInfos;
    imageInfos.reserve(images.size());
    for (auto &img : images) {
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
  // Default-constructing fastgltf::Parser enables ZERO extensions, so any
  // glTF/GLB whose extensionsRequired lists something (KHR_materials_*,
  // KHR_texture_transform, etc. - common in Blender-exported low-poly
  // asset packs like the tree models) fails to parse with "One or more
  // extensions are required by the glTF but not enabled in the Parser."
  // This isn't model-specific - it'll happen for any future asset that
  // uses an extension not in this list, so if a new failure names a
  // different extension, add it here rather than special-casing the file.
  fastgltf::Parser parser(
      fastgltf::Extensions::KHR_texture_transform |
      fastgltf::Extensions::KHR_texture_basisu |
      fastgltf::Extensions::KHR_mesh_quantization |
      fastgltf::Extensions::EXT_meshopt_compression |
      fastgltf::Extensions::KHR_lights_punctual |
      fastgltf::Extensions::KHR_materials_specular |
      fastgltf::Extensions::KHR_materials_ior |
      fastgltf::Extensions::KHR_materials_volume |
      fastgltf::Extensions::KHR_materials_transmission |
      fastgltf::Extensions::KHR_materials_clearcoat |
      fastgltf::Extensions::KHR_materials_emissive_strength |
      fastgltf::Extensions::KHR_materials_sheen |
      fastgltf::Extensions::KHR_materials_unlit |
      fastgltf::Extensions::KHR_materials_anisotropy |
      fastgltf::Extensions::KHR_materials_variants |
      fastgltf::Extensions::EXT_mesh_gpu_instancing |
      fastgltf::Extensions::KHR_draco_mesh_compression |
      fastgltf::Extensions::KHR_materials_iridescence |
      fastgltf::Extensions::KHR_materials_dispersion |
      fastgltf::Extensions::MSFT_texture_dds |
      fastgltf::Extensions::EXT_texture_webp);
  auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(),
                               fastgltf::Options::LoadExternalBuffers |
                                   fastgltf::Options::LoadExternalImages);
  if (asset.error() != fastgltf::Error::None) {
    // fastgltf::getErrorMessage() doesn't name which extension(s) are
    // missing, so pull extensionsRequired straight out of the file's raw
    // JSON to report it directly instead of leaving you to guess.
    auto required = readExtensionsRequired(gltfFile.get());
    std::string requiredList =
        required.empty() ? "(none listed / couldn't read)" : "";
    for (size_t i = 0; i < required.size(); ++i) {
      requiredList += required[i];
      if (i + 1 < required.size())
        requiredList += ", ";
    }
    throw std::runtime_error(
        std::format("Failed to parse GLB: {} (extensionsRequired in file: {})",
                    fastgltf::getErrorMessage(asset.error()), requiredList));
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
                indices.emplace_back(idx + static_cast<uint32_t>(initial_vtx));
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
              gltf, uvAccessor, [this, initial_vtx](glm::vec2 v, size_t index) {
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
  lodMeshGroups = groupMeshesByLodName(gltf);

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
    // ImageCreateInfo::format defaults to vk::Format::eR8G8B8A8Srgb (see
    // image.hpp), which is correct for base-color textures but WRONG for
    // normal maps and metallic-roughness textures - those are data, not
    // color, and gamma-decoding them distorts the actual normal/roughness/
    // metallic values Blender (and the glTF spec) treat as linear. Images
    // are loaded generically below with no per-image format override, so
    // every texture was getting sRGB regardless of role - flattening
    // normal-map detail and skewing roughness/metallic values, which reads
    // as "flatter/grayer than Blender" exactly like what's being seen.
    //
    // Role isn't known until materials are parsed, but images are loaded
    // before that (materials reference them by index, not the reverse) -
    // so scan materials first to tag which image indices are used as
    // normal/metallicRoughness maps, then use that below when creating
    // each image, instead of always taking ImageCreateInfo's sRGB default.
    std::vector<bool> imageIsLinearData(gltf.images.size(), false);
    for (auto &material : gltf.materials) {
      if (material.normalTexture.has_value()) {
        auto &tex = gltf.textures[material.normalTexture.value().textureIndex];
        if (tex.imageIndex.has_value())
          imageIsLinearData[tex.imageIndex.value()] = true;
      }
      if (material.pbrData.metallicRoughnessTexture.has_value()) {
        auto &tex =
            gltf.textures[material.pbrData.metallicRoughnessTexture.value()
                              .textureIndex];
        if (tex.imageIndex.has_value())
          imageIsLinearData[tex.imageIndex.value()] = true;
      }
    }

    size_t imageIdx = 0;
    for (auto &image : gltf.images) {
      vk::Format imageFormat =
          imageIdx < imageIsLinearData.size() && imageIsLinearData[imageIdx]
              ? vk::Format::eR8G8B8A8Unorm
              : vk::Format::eR8G8B8A8Srgb;

      // `imageIdx` must walk in lockstep with `gltf.images`, because both
      // `imageIsLinearData` above and every Material::*TextureIndex set
      // from tex.imageIndex below are indexed against gltf.images' order,
      // while `images` (built here) is what actually becomes the
      // descriptor array bound as sceneTextureSet. Every branch that
      // matches a source variant MUST push exactly one entry to `images`,
      // or the two orderings silently drift apart and every texture index
      // after the skipped one points at the wrong slot for the rest of
      // the asset (e.g. a normal map read as an albedo, purple/blue
      // tinted geometry, or an untextured/garbage-looking mesh reading
      // whatever was left in an unrelated descriptor slot).
      bool imagePushed = false;

      // Constructing an Image2D can throw (e.g. stbi failing to decode
      // bytes that aren't actually PNG/JPEG - see image.cpp's
      // ImageCreateInfo_PNGdata constructor). That used to propagate
      // straight out of uploadModelGPU(), which meant: (a) every OTHER
      // image in this asset after the failed one never got its images
      // vector entry either, cascading the exact index-drift problem
      // described above, and (b) every log statement below this loop
      // (the per-Scene material/mesh summary) silently never ran, making
      // a real decode failure look identical to "nothing happened".
      // Catch per-image instead: log which glTF image/index failed and
      // why, then substitute an obvious magenta placeholder so `images`
      // stays the same length as `gltf.images` and the rest of this
      // asset (and the logging below) still loads normally.
      auto pushImageOrPlaceholder = [&](auto makeCreateInfo) {
        try {
          images.emplace_back(context, makeCreateInfo());
        } catch (const std::exception &e) {
          std::println(
              stderr,
              "[Scene] image[{}] (name='{}') failed to load: {} - "
              "substituting a magenta placeholder so texture indices for "
              "the REST of this asset stay aligned.",
              imageIdx, image.name, e.what());
          ImageCreateInfo_color placeholder{};
          placeholder.format = imageFormat;
          placeholder.size = {1, 1};
          placeholder.color = 0xFFFF00FFu; // opaque magenta, RGBA8
          images.emplace_back(context, placeholder);
        }
        imagePushed = true;
      };

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
                          pushImageOrPlaceholder([&] {
                            ImageCreateInfo_PNGdata createInfo;
                            createInfo.format = imageFormat;
                            createInfo.data = reinterpret_cast<void *>(
                                vector.bytes.data() + bufferView.byteOffset);
                            createInfo.dataSize =
                                static_cast<size_t>(bufferView.byteLength);
                            return createInfo;
                          });
                        }},
                    buffer.data);
              },
              // Image bytes embedded directly on the fastgltf::Image, with
              // no bufferView indirection - e.g. an external URI image
              // that fastgltf read into memory itself because we pass
              // Options::LoadExternalImages, or a GLB image fastgltf
              // decided to expose this way. Same encoded-bytes path as
              // the BufferView case above, just missing the extra
              // bufferView/buffer hop. Previously this fell into the
              // no-op `[](auto&){}` arm above and silently produced the
              // imageIdx drift described above.
              [&](fastgltf::sources::Array &vector) {
                pushImageOrPlaceholder([&] {
                  ImageCreateInfo_PNGdata createInfo;
                  createInfo.format = imageFormat;
                  createInfo.data =
                      reinterpret_cast<void *>(vector.bytes.data());
                  createInfo.dataSize = vector.bytes.size();
                  return createInfo;
                });
              },
          },
          image.data);

      // Fail loudly instead of silently corrupting every later texture
      // index in this asset - see the comment above.
      if (!imagePushed) {
        throw std::runtime_error(std::format(
            "Scene: unhandled embedded-image source for image '{}' "
            "(index {}) - only BufferView- and Array-encoded images are "
            "currently supported. Add a visitor arm for this source type "
            "instead of letting it silently drop.",
            image.name, imageIdx));
      }

      imageIdx++;
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

    // std::println("[Scene] {} gltf image(s) -> {} loaded image(s), {} "
    //              "material(s), {} mesh(es)",
    //              gltf.images.size(), images.size(), materials.size(),
    //              meshes.size());
    if (images.size() != gltf.images.size()) {
      std::println(
          "[Scene]   WARNING: images.size() ({}) != gltf.images.size() ({}) "
          "- every material's texture index below is misaligned against "
          "the actual descriptor array!",
          images.size(), gltf.images.size());
    }
    for (size_t i = 0; i < materials.size(); ++i) {
      auto &m = materials[i];
      // std::println(
      //     "[Scene]   material[{}] baseColorTex={} mrTex={} normalTex={} "
      //     "baseColorFactor=({:.2f},{:.2f},{:.2f},{:.2f}) roughness={:.2f}",
      //     i, m.baseColorTextureIndex.value_or(-1),
      //     m.metallicRoughnessTextureIndex.value_or(-1),
      //     m.normalTextureIndex.value_or(-1), m.baseColorFactor.x,
      //     m.baseColorFactor.y, m.baseColorFactor.z, m.baseColorFactor.w,
      //     m.roughnessFactor);
    }
    for (size_t i = 0; i < meshes.size(); ++i) {
      for (size_t p = 0; p < meshes[i].primitives.size(); ++p) {
        auto &prim = meshes[i].primitives[p];
        // std::println("[Scene]   mesh[{}] primitive[{}] materialIndex={} "
        //              "indexOffset={} indexCount={}",
        //              i, p, prim.materialIndex, prim.indexOffset,
        //              prim.indexCount);
      }
      //   std::println("[Scene]   mesh[{}] local AABB
      //   min=({:.2f},{:.2f},{:.2f}) "
      //                "max=({:.2f},{:.2f},{:.2f}) skinIndex={}",
      //                i, meshes[i].aabb.min.x, meshes[i].aabb.min.y,
      //                meshes[i].aabb.min.z, meshes[i].aabb.max.x,
      //                meshes[i].aabb.max.y, meshes[i].aabb.max.z,
      //                meshes[i].skinIndex.has_value()
      //                    ? static_cast<int>(meshes[i].skinIndex.value())
      //                    : -1);
    }
    // std::println("[Scene] {} node(s), {} root node(s): {}", nodes.size(),
    //              rootNodes.size(), [&] {
    //                std::string s;
    //                for (size_t i = 0; i < rootNodes.size(); ++i) {
    //                  s += std::to_string(rootNodes[i]);
    //                  if (i + 1 < rootNodes.size())
    //                    s += ", ";
    //                }
    //                return s;
    //              }());
    if (lodMeshGroups.empty()) {
      // std::println("[Scene]   no \"_LOD<N>\"-named nodes found (expected if "
      //              "this asset doesn't use LOD).");
    } else {
      for (size_t level = 0; level < lodMeshGroups.size(); ++level) {
        std::string meshList;
        for (size_t k = 0; k < lodMeshGroups[level].size(); ++k) {
          meshList += std::to_string(lodMeshGroups[level][k]);
          if (k + 1 < lodMeshGroups[level].size())
            meshList += ", ";
        }
        std::println("[Scene]   LOD{}: mesh(es) [{}]{}", level, meshList,
                     level == 0 ? " (most detailed)" : "");
      }

      // Default distance thresholds, only if the caller hasn't already set
      // some (e.g. re-loading, or a caller that sets this before use).
      // Scaled off LOD0's own bounding-sphere diameter so the switch
      // distances self-adjust to THIS asset's actual size instead of using
      // a fixed number that would look wrong on anything bigger/smaller
      // than whatever this was first tuned against. This is a generic
      // placeholder, not real tuning - see lodDistances' doc comment in
      // scene.hpp for how to override it per-asset.
      if (lodDistances.empty() && !lodMeshGroups[0].empty()) {
        auto &aabb = meshes[lodMeshGroups[0][0]].aabb;
        float diameter = glm::length(aabb.max - aabb.min);
        for (size_t level = 0; level + 1 < lodMeshGroups.size(); ++level) {
          lodDistances.push_back(diameter * 20.f *
                                 static_cast<float>(level + 1));
        }
        std::string distList;
        for (size_t k = 0; k < lodDistances.size(); ++k) {
          distList += std::format("{:.1f}", lodDistances[k]);
          if (k + 1 < lodDistances.size())
            distList += ", ";
        }
        std::println("[Scene]   LOD default distance thresholds: [{}] "
                     "(derived from LOD0's bounding size - override "
                     "Scene::lodDistances directly if these look wrong)",
                     distList);
      }
    }
    std::fflush(stdout); // in case stdout is fully-buffered and the app
                         // exits/crashes before its next natural flush -
                         // don't let a buffering quirk look like "nothing
                         // was logged".

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

  Buffer<std::byte> stagingBuffer(context,
                                  vk::BufferUsageFlagBits::eTransferSrc,
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
