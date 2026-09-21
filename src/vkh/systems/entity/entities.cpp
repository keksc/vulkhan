#include "entities.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include <print>
#include <vulkan/vulkan.hpp>

namespace vkh {

void EntitySys::createSetLayouts() {
  {
    std::vector<vk::DescriptorBindingFlags> bindingFlags = {
        vk::DescriptorBindingFlagBits::ePartiallyBound};

    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{
            0,                                         // binding
            vk::DescriptorType::eCombinedImageSampler, // descriptorType
            256, // descriptorCount (Max textures per scene array)
            vk::ShaderStageFlagBits::eFragment, // stageFlags
            nullptr                             // pImmutableSamplers
        }};

    texturesSetLayout = buildDescriptorSetLayout(
        context, bindings, vk::DescriptorSetLayoutCreateFlags{},
        &bindingFlagsInfo);
  }

  {
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex}};

    instanceSetLayout = buildDescriptorSetLayout(context, bindings);
  }
}

void EntitySys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, texturesSetLayout,
      instanceSetLayout};

  // Water-plane clip for PlanarReflectionSys's reflection pass - see
  // render()'s clipPlane parameter and entities.vert's PushConstants
  // block. eVertex only: entities.vert is the only stage that reads it
  // (writes gl_ClipDistance[0], which the rasterizer then acts on before
  // the fragment stage ever runs - the fragment shader itself never
  // needs this value).
  vk::PushConstantRange clipPlaneRange{};
  clipPlaneRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
  clipPlaneRange.offset = 0;
  clipPlaneRange.size = sizeof(glm::vec4);

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &clipPlaneRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/entity/entities.vert.spv";
  pipelineInfo.fragpath = "shaders/entity/entities.frag.spv";

  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "entities");
}

void EntitySys::createCullingPipeline() {
  // Bindings shared by both culling passes:
  //  0 CullingUbo                 1 InstanceBuffer (raw, CPU-authored)
  //  2 CompactedInstanceBuffer    3 GroupFirstInstanceBuffer
  //  4 GroupVisibleCountBuffer    5 CommandGroupIndexBuffer
  //  6 IndirectBuffer (draw commands)
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute}};

  cullingSetLayout = buildDescriptorSetLayout(context, bindings);

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;

  cullingPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/entity/culling.comp.spv", layoutInfo,
      "culling compute");

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  cullingDescriptorSets.resize(framesInFlight);
  cullingUboBuffers.resize(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; i++) {
    cullingUboBuffers[i] = std::make_unique<Buffer<CullingUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);
  }
}

void EntitySys::createFinalizePipeline() {
  // Reuses cullingSetLayout - same bindings, just a different shader that
  // only touches bindings 0, 4, 5, 6.
  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;

  finalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/entity/cullingFinalize.comp.spv", layoutInfo,
      "culling finalize compute");
}

EntitySys::EntitySys(EngineContext &context) : System(context) {
  createSetLayouts();
  createPipeline();
  createCullingPipeline();
  createFinalizePipeline();

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  instanceBuffers.resize(framesInFlight);
  indirectDrawBuffers.resize(framesInFlight);
  jointBuffers.resize(framesInFlight);
  compactedInstanceBuffers.resize(framesInFlight);
  groupFirstInstanceBuffers.resize(framesInFlight);
  commandGroupIndexBuffers.resize(framesInFlight);
  groupVisibleCountBuffers.resize(framesInFlight);
  instanceDescriptorSets.resize(framesInFlight, nullptr);
  framesDirty.resize(framesInFlight, false);
}

EntitySys::~EntitySys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(texturesSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(instanceSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(cullingSetLayout, nullptr);
  }
}

void EntitySys::updateJoints() {
  if (entities.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;

  flushBuffers(frameIndex);

  if (jointBuffers.empty() || !jointBuffers[frameIndex])
    return;

  std::vector<glm::mat4> jointData;
  for (auto &entity : entities) {
    auto &mesh = entity.getMesh();
    if (mesh.skinIndex.has_value()) {
      auto &skin = entity.scene->skins[mesh.skinIndex.value()];
      for (size_t i = 0; i < skin.joints.size(); ++i) {
        glm::mat4 jointMatrix =
            entity.scene->nodes[skin.joints[i]].globalTransform *
            skin.inverseBindMatrices[i];
        jointData.push_back(jointMatrix);
      }
    }
  }

  if (!jointData.empty()) {
    jointBuffers[frameIndex]->map();
    jointBuffers[frameIndex]->write(jointData.data(),
                                    jointData.size() * sizeof(glm::mat4));
    jointBuffers[frameIndex]->unmap();
  }
}

void EntitySys::updateBuffers() {
  if (entities.empty()) {
    cpuInstanceData.clear();
    cpuDrawCommands.clear();
    sceneBatches.clear();
    cpuJointData.clear();
    cpuGroupFirstInstance.clear();
    cpuCommandGroupIndex.clear();
    for (size_t i = 0; i < framesDirty.size(); ++i)
      framesDirty[i] = true;
    return;
  }

  // A LOD-enabled entity's effective mesh depends on its CURRENT distance
  // to the camera, which can change every frame even though nothing
  // structural (entity add/remove) happened - so the ordinary
  // structuralDirty caching below can't apply to it: the set of groups/
  // draw commands needed can differ frame to frame (e.g. all trees far
  // away needs only the least-detailed level's groups; camera moving
  // closer suddenly needs more). Cheapest correct fix at the entity counts
  // this runs at (low hundreds): whenever ANY scene with lodMeshGroups is
  // present, just force a full rebuild of cpuDrawCommands/
  // cpuGroupFirstInstance/cpuCommandGroupIndex/sceneBatches EVERY frame,
  // for every scene (not just the LOD one) - simpler than partitioning the
  // rebuild per-scene, and free of extra GPU cost since flushBuffers()
  // already re-uploads everything every frame regardless (framesDirty is
  // unconditionally forced true below already). This does NOT touch the
  // structuralDirty MEMBER itself - that still only reflects real
  // entity add/remove, for markStructuralDirty()'s own callers.
  bool anyLodScenes = false;
  for (auto &e : entities) {
    if (!e.scene->lodMeshGroups.empty()) {
      anyLodScenes = true;
      break;
    }
  }
  bool rebuildStructure = structuralDirty || anyLodScenes;

  if (rebuildStructure) {
    cpuDrawCommands.clear();
    sceneBatches.clear();
    cpuGroupFirstInstance.clear();
    cpuCommandGroupIndex.clear();
  }
  cpuInstanceData.clear();
  cpuJointData.clear();

  uint32_t currentJointOffset = 0;
  uint32_t groupCounter = 0; // one entry per (scene,mesh,PRIMITIVE) below -
                             // see the per-primitive loop's comment on why
                             // it's not just (scene,mesh) - every frame in
                             // the same order, so this index is stable even
                             // on frames that skip rebuilding
                             // cpuGroupFirstInstance/cpuDrawCommands

  // Some glTF assets (e.g. ones with no materials defined at all) can leave
  // `materials` empty even though a primitive's materialIndex defaults to
  // 0 — guard both that and an empty primitive list rather than indexing
  // out of bounds with operator[]. Hoisted out of the per-entity loop below
  // since both the LOD and non-LOD paths need it identically.
  static const Scene<Vertex>::Material kDefaultMaterial{
      .baseColorFactor = glm::vec4(1.f),
      .roughnessFactor = 1.f,
      .metallicFactor = glm::vec4(0.f),
  };

  for (size_t i = 0; i < entities.size();) {
    auto currentScene = entities[i].scene;
    uint32_t firstDrawOffset = static_cast<uint32_t>(cpuDrawCommands.size());
    uint32_t drawCount = 0;

    size_t sceneRunEnd = i;
    while (sceneRunEnd < entities.size() &&
           entities[sceneRunEnd].scene == currentScene)
      ++sceneRunEnd;

    if (!currentScene->lodMeshGroups.empty()) {
      // ---- LOD-enabled scene: bucket this run's entities by a freshly
      // DISTANCE-selected level, not by their static (fixed-at-creation)
      // meshIndex - which mesh actually gets drawn for a given entity
      // changes as the camera moves relative to it. See rebuildStructure's
      // comment above for why this whole branch can't be cached.
      glm::vec3 camPos = glm::vec3(glm::inverse(context.camera.viewMatrix)[3]);

      // Culling AABB for this scene's entities is computed from LOD0 (the
      // most detailed level) directly, via lodMeshGroups[0] - NOT via
      // entities[e].getWorldAABB() (which indexes scene->meshes using the
      // entity's own fixed meshIndex field, see entity.cpp). That field's
      // value for these entities depends entirely on whatever main.cpp
      // happened to pass at entity-creation time, which this code has no
      // way to verify - and using the WRONG mesh's AABB here wouldn't fail
      // loudly, it would just silently mis-cull LOD trees (popping in/out
      // of view at the wrong distance). Building it directly from
      // lodMeshGroups[0] instead makes correctness independent of that
      // external assumption entirely. Unioned across every mesh in
      // lodMeshGroups[0] in case that level's parts (trunk/leaves/etc)
      // weren't merged into a single multi-primitive mesh before export.
      //
      // NOTE: this assumes lodMeshGroups[0] (and every other level) is
      // actually populated - i.e. the export produces real, distinct
      // geometry per "_LODN" level rather than collapsing them into one
      // merged mesh under a single name. If that assumption breaks again
      // in the future (check the "[Scene] LODn: mesh(es) [...]" log line
      // for an empty level), this will go back to drawing nothing for
      // whichever level ends up empty - see this function's git history
      // for the nearest-non-empty-level fallback that was here
      // temporarily while the export was broken, since re-adding it
      // masks exactly the kind of export mistake worth seeing immediately
      // instead of silently working around.
      auto computeLod0WorldAABB = [&](const glm::mat4 &entityMat) {
        AABB worldAABB;
        for (size_t meshIdx : currentScene->lodMeshGroups[0]) {
          auto &lod0Mesh = currentScene->meshes[meshIdx];
          glm::mat4 modelMatrix = entityMat * lod0Mesh.transform;
          glm::vec3 corners[8] = {
              {lod0Mesh.aabb.min.x, lod0Mesh.aabb.min.y, lod0Mesh.aabb.min.z},
              {lod0Mesh.aabb.min.x, lod0Mesh.aabb.min.y, lod0Mesh.aabb.max.z},
              {lod0Mesh.aabb.min.x, lod0Mesh.aabb.max.y, lod0Mesh.aabb.min.z},
              {lod0Mesh.aabb.min.x, lod0Mesh.aabb.max.y, lod0Mesh.aabb.max.z},
              {lod0Mesh.aabb.max.x, lod0Mesh.aabb.min.y, lod0Mesh.aabb.min.z},
              {lod0Mesh.aabb.max.x, lod0Mesh.aabb.min.y, lod0Mesh.aabb.max.z},
              {lod0Mesh.aabb.max.x, lod0Mesh.aabb.max.y, lod0Mesh.aabb.min.z},
              {lod0Mesh.aabb.max.x, lod0Mesh.aabb.max.y, lod0Mesh.aabb.max.z},
          };
          for (auto &corner : corners) {
            glm::vec3 worldPos =
                glm::vec3(modelMatrix * glm::vec4(corner, 1.f));
            worldAABB.min = glm::min(worldAABB.min, worldPos);
            worldAABB.max = glm::max(worldAABB.max, worldPos);
          }
        }
        return worldAABB;
      };

      std::vector<std::vector<size_t>> entitiesPerLevel(
          currentScene->lodMeshGroups.size());
      for (size_t e = i; e < sceneRunEnd; ++e) {
        float dist = glm::distance(camPos, entities[e].transform.position);
        size_t level = currentScene->lodMeshGroups.size() - 1; // default:
                                                               // least
                                                               // detailed
        for (size_t L = 0; L < currentScene->lodDistances.size(); ++L) {
          if (dist < currentScene->lodDistances[L]) {
            level = L;
            break;
          }
        }
        entitiesPerLevel[level].push_back(e);
      }

      for (size_t level = 0; level < entitiesPerLevel.size(); ++level) {
        auto &levelEntities = entitiesPerLevel[level];
        if (levelEntities.empty())
          continue;
        uint32_t instanceCount = static_cast<uint32_t>(levelEntities.size());

        for (size_t meshIdx : currentScene->lodMeshGroups[level]) {
          auto &mesh = currentScene->meshes[meshIdx];

          uint32_t thisGroupJointOffset = currentJointOffset;
          if (mesh.skinIndex.has_value()) {
            auto &skin = currentScene->skins[mesh.skinIndex.value()];
            for (size_t s = 0; s < skin.joints.size(); ++s) {
              cpuJointData.push_back(
                  currentScene->nodes[skin.joints[s]].globalTransform *
                  skin.inverseBindMatrices[s]);
            }
            currentJointOffset += static_cast<uint32_t>(skin.joints.size());
          }

          for (size_t primIdx = 0; primIdx < mesh.primitives.size();
               ++primIdx) {
            auto &primitive = mesh.primitives[primIdx];
            uint32_t meshInstanceStart =
                static_cast<uint32_t>(cpuInstanceData.size());
            uint32_t groupIdx = groupCounter++;

            if (rebuildStructure) {
              cpuGroupFirstInstance.push_back(meshInstanceStart);

              vk::DrawIndexedIndirectCommand cmd{};
              cmd.indexCount = primitive.indexCount;
              cmd.instanceCount = instanceCount;
              cmd.firstIndex = primitive.indexOffset;
              cmd.vertexOffset = 0;
              cmd.firstInstance = meshInstanceStart;

              cpuDrawCommands.push_back(cmd);
              cpuCommandGroupIndex.push_back(groupIdx);
              drawCount++;
            }

            const Scene<Vertex>::Material *mat = &kDefaultMaterial;
            size_t matIdx = primitive.materialIndex;
            if (matIdx < currentScene->materials.size())
              mat = &currentScene->materials[matIdx];

            int32_t texIdx = mat->baseColorTextureIndex.value_or(-1);
            int32_t mrTexIdx = mat->metallicRoughnessTextureIndex.value_or(-1);

            for (size_t entIdx : levelEntities) {
              // Computed from LOD0 directly (see computeLod0WorldAABB's
              // comment above) - stays stable and conservative regardless
              // of which level is actually being drawn for this instance,
              // and doesn't depend on entity.meshIndex being set to
              // anything in particular.
              AABB worldAABB =
                  computeLod0WorldAABB(entities[entIdx].transform.mat4());

              GPUInstanceData data;
              data.modelMatrix =
                  entities[entIdx].transform.mat4() * mesh.transform;
              data.normalMatrix =
                  glm::mat4(entities[entIdx].transform.normalMatrix());
              data.color = entities[entIdx].color * mat->baseColorFactor;
              data.aabbMin = worldAABB.min;
              data.aabbMax = worldAABB.max;
              data.textureIndex = texIdx;
              data.metallicRoughnessTextureIndex = mrTexIdx;
              data.roughnessFactor = mat->roughnessFactor;
              data.metallicFactor = mat->metallicFactor.x;
              data.jointOffset =
                  mesh.skinIndex.has_value()
                      ? static_cast<int32_t>(thisGroupJointOffset)
                      : -1;
              data.groupIndex = static_cast<int32_t>(groupIdx);

              cpuInstanceData.push_back(data);
            }
          }
        }
      }

      if (rebuildStructure && drawCount > 0) {
        SceneBatch batch{};
        batch.scene = currentScene;
        batch.firstDrawCommandOffset = firstDrawOffset;
        batch.drawCommandCount = drawCount;
        sceneBatches.push_back(batch);
      }
      i = sceneRunEnd;
      continue;
    }

    size_t j = i;
    while (j < entities.size() && entities[j].scene == currentScene) {
      size_t k = j;
      while (k < entities.size() && entities[k].scene == currentScene &&
             entities[k].meshIndex == entities[j].meshIndex) {
        k++;
      }

      uint32_t instanceCount = static_cast<uint32_t>(k - j);
      auto &entity = entities[j];
      auto &mesh = entity.getMesh();

      // Gather this MESH's joint matrices exactly once here (not once per
      // instance, and not once per primitive below) and capture the offset
      // before bumping currentJointOffset, so every instance of every
      // primitive points at the same pose - matches the assumption below
      // that all instances of one skinned mesh share one skeletal pose.
      uint32_t thisGroupJointOffset = currentJointOffset;
      if (mesh.skinIndex.has_value()) {
        auto &skin = currentScene->skins[mesh.skinIndex.value()];
        for (size_t s = 0; s < skin.joints.size(); ++s) {
          cpuJointData.push_back(
              currentScene->nodes[skin.joints[s]].globalTransform *
              skin.inverseBindMatrices[s]);
        }
        currentJointOffset += static_cast<uint32_t>(skin.joints.size());
      }

      // A mesh can have several PRIMITIVES, each with its OWN material
      // (different base-color/normal/metallic-roughness texture, different
      // roughness/metallic factors) - e.g. this tree asset's trunk-bark,
      // leaf, and second-bark materials, each with its own UVs authored
      // against its own texture. GPUInstanceData has exactly one texture/
      // roughness/metallic slot per entry, so one entry can only describe
      // ONE primitive's material. This used to be built ONCE per mesh using
      // only mesh.primitives[0]'s material, then reused for every OTHER
      // primitive's draw command too - every primitive's fragment shader
      // read the trunk's texture/roughness/metallic regardless of which
      // primitive (and which UVs) it was actually rasterizing. A primitive
      // authored for a leaf atlas sampling a bark atlas at leaf UV
      // coordinates mostly reads wrong/dark texels - exactly the
      // "uniformly dark, no distinct brown/green" look.
      //
      // Fix: make each PRIMITIVE its own group (own draw command, own
      // instance range, own groupIndex for culling), duplicating this run's
      // transform/AABB data once per primitive instead of once per entity-
      // run. Costs extra instance-buffer/culling work proportional to
      // primitive count (a 3-primitive tree now takes 3x the instance slots
      // it did before), but is by far the smallest change that's actually
      // correct - the alternative (a single per-DRAW-COMMAND material
      // buffer indexed by gl_DrawID in the shader, avoiding the
      // duplication) needs a device feature (shaderDrawParameters) this
      // codebase doesn't currently enable, so it's out of scope here.
      for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
        auto &primitive = mesh.primitives[primIdx];
        uint32_t meshInstanceStart =
            static_cast<uint32_t>(cpuInstanceData.size());
        uint32_t groupIdx = groupCounter++;

        if (rebuildStructure) {
          cpuGroupFirstInstance.push_back(meshInstanceStart);

          vk::DrawIndexedIndirectCommand cmd{};
          cmd.indexCount = primitive.indexCount;
          cmd.instanceCount = instanceCount;
          cmd.firstIndex = primitive.indexOffset;
          cmd.vertexOffset = 0;
          cmd.firstInstance = meshInstanceStart;

          cpuDrawCommands.push_back(cmd);
          cpuCommandGroupIndex.push_back(groupIdx);
          drawCount++;
        }

        const Scene<Vertex>::Material *mat = &kDefaultMaterial;
        size_t matIdx = primitive.materialIndex;
        if (matIdx < currentScene->materials.size())
          mat = &currentScene->materials[matIdx];

        int32_t texIdx = mat->baseColorTextureIndex.value_or(-1);
        int32_t mrTexIdx = mat->metallicRoughnessTextureIndex.value_or(-1);

        for (size_t inst = j; inst < k; ++inst) {
          AABB worldAABB = entities[inst].getWorldAABB();

          GPUInstanceData data;
          data.modelMatrix = entities[inst].transform.mat4() * mesh.transform;
          data.normalMatrix =
              glm::mat4(entities[inst].transform.normalMatrix());
          data.color = entities[inst].color * mat->baseColorFactor;
          data.aabbMin = worldAABB.min;
          data.aabbMax = worldAABB.max;
          data.textureIndex = texIdx;
          data.metallicRoughnessTextureIndex = mrTexIdx;
          data.roughnessFactor = mat->roughnessFactor;
          data.metallicFactor = mat->metallicFactor.x;
          data.jointOffset = mesh.skinIndex.has_value()
                                 ? static_cast<int32_t>(thisGroupJointOffset)
                                 : -1;
          data.groupIndex = static_cast<int32_t>(groupIdx);

          cpuInstanceData.push_back(data);
        }
      }

      j = k;
    }

    if (rebuildStructure && drawCount > 0) {
      SceneBatch batch{};
      batch.scene = currentScene;
      batch.firstDrawCommandOffset = firstDrawOffset;
      batch.drawCommandCount = drawCount;
      sceneBatches.push_back(batch);
    }
    i = j;
  }

  // DEBUG LOGGING - intentionally keys off structuralDirty, NOT
  // rebuildStructure: once any LOD-enabled scene exists, rebuildStructure
  // is true every single frame (see its own comment above), and logging
  // this every frame would be permanent console spam instead of the rare,
  // meaningful event it's meant to be. Call markStructuralDirty() once if
  // you need a fresh dump of the LOD groups after changing something.
  // Dumps every (scene,mesh) group's instance range plus the world
  // position of up to the first 3 instances in that group, so a group
  // that shouldn't exist, an instanceCount that doesn't match what was
  // placed, or a cluster of instances unexpectedly sitting at/near the
  // origin shows up directly in the log instead of only as a visual
  // artifact.
  if (structuralDirty) {
    // std::println("[EntitySys] rebuilt: {} entities -> {} groups, {} draw "
    //              "command(s), {} scene batch(es)",
    //              entities.size(), cpuGroupFirstInstance.size(),
    //              cpuDrawCommands.size(), sceneBatches.size());
    for (size_t g = 0; g < cpuGroupFirstInstance.size(); ++g) {
      uint32_t firstInst = cpuGroupFirstInstance[g];
      uint32_t nextFirst = (g + 1 < cpuGroupFirstInstance.size())
                               ? cpuGroupFirstInstance[g + 1]
                               : static_cast<uint32_t>(cpuInstanceData.size());
      uint32_t count = nextFirst - firstInst;
      // std::println("[EntitySys]   group[{}] firstInstance={} count={}", g,
      //              firstInst, count);
      for (uint32_t s = 0; s < std::min<uint32_t>(count, 3); ++s) {
        auto &m = cpuInstanceData[firstInst + s].modelMatrix;
        glm::vec3 pos = glm::vec3(m[3]);
        // std::println("[EntitySys]     instance[{}] worldPos=({:.2f},{:.2f},{"
        //              ":.2f}) textureIndex={}",
        //              s, pos.x, pos.y, pos.z,
        //              cpuInstanceData[firstInst + s].textureIndex);
      }
    }
  }

  int frameIndex = context.frameInfo.frameIndex;
  auto planes = camera::getFrustumPlanes(context.camera.projectionMatrix *
                                         context.camera.viewMatrix);
  CullingUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  ubo.totalCommands = static_cast<uint32_t>(cpuDrawCommands.size());

  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  structuralDirty = false;
  for (size_t i = 0; i < framesDirty.size(); ++i)
    framesDirty[i] = true;
}

void EntitySys::flushBuffers(int frameIndex) {
  if (!framesDirty[frameIndex])
    return;

  vk::DeviceSize instanceBufferSize =
      cpuInstanceData.size() * sizeof(GPUInstanceData);
  vk::DeviceSize jointBufferSize = std::max<vk::DeviceSize>(
      cpuJointData.size() * sizeof(glm::mat4), sizeof(glm::mat4));
  vk::DeviceSize cmdBufferSize =
      cpuDrawCommands.size() * sizeof(vk::DrawIndexedIndirectCommand);
  uint32_t groupCount = static_cast<uint32_t>(cpuGroupFirstInstance.size());
  uint32_t commandCount = static_cast<uint32_t>(cpuCommandGroupIndex.size());

  bool updateGraphicsDescriptor = false;
  bool updateCullingDescriptor = false;

  if (!instanceBuffers[frameIndex] ||
      instanceBuffers[frameIndex]->getSize() < instanceBufferSize) {
    instanceBuffers[frameIndex] = std::make_unique<Buffer<GPUInstanceData>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(cpuInstanceData.size(), 1));
    updateCullingDescriptor = true;
  }

  // Device-local: only ever written by the culling compute pass, read by
  // the vertex shader. Sized to match the raw instance buffer.
  if (!compactedInstanceBuffers[frameIndex] ||
      compactedInstanceBuffers[frameIndex]->getSize() < instanceBufferSize) {
    compactedInstanceBuffers[frameIndex] =
        std::make_unique<Buffer<GPUInstanceData>>(
            context, vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            std::max<uint32_t>(cpuInstanceData.size(), 1));
    updateGraphicsDescriptor = true;
    updateCullingDescriptor = true;
  }

  if (!groupFirstInstanceBuffers[frameIndex] ||
      groupFirstInstanceBuffers[frameIndex]->getSize() <
          groupCount * sizeof(uint32_t)) {
    groupFirstInstanceBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(groupCount, 1));
    updateCullingDescriptor = true;
  }

  if (!groupVisibleCountBuffers[frameIndex] ||
      groupVisibleCountBuffers[frameIndex]->getSize() <
          groupCount * sizeof(uint32_t)) {
    // eTransferDst: dispatchCulling() now resets this buffer to 0 via
    // vkCmdFillBuffer immediately before every cull dispatch (see its own
    // comment) instead of relying on a one-time CPU-side zero - fillBuffer
    // requires the buffer to have been created with this usage bit.
    groupVisibleCountBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(groupCount, 1));
    updateCullingDescriptor = true;
  }

  if (!commandGroupIndexBuffers[frameIndex] ||
      commandGroupIndexBuffers[frameIndex]->getSize() <
          commandCount * sizeof(uint32_t)) {
    commandGroupIndexBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(commandCount, 1));
    updateCullingDescriptor = true;
  }

  if (!indirectDrawBuffers[frameIndex] ||
      indirectDrawBuffers[frameIndex]->getSize() < cmdBufferSize) {
    indirectDrawBuffers[frameIndex] =
        std::make_unique<Buffer<vk::DrawIndexedIndirectCommand>>(
            context,
            vk::BufferUsageFlagBits::eIndirectBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent,
            std::max<uint32_t>(cpuDrawCommands.size(), 1));
    updateCullingDescriptor = true;
  }

  if (!jointBuffers[frameIndex] ||
      jointBuffers[frameIndex]->getSize() < jointBufferSize) {
    jointBuffers[frameIndex] = std::make_unique<Buffer<glm::mat4>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(cpuJointData.size(), 1));
    updateGraphicsDescriptor = true;
  }

  // Graphics-side set: vertex shader now reads the *compacted* instance
  // buffer (culling-pass output), not the raw CPU-authored one.
  if (updateGraphicsDescriptor || !instanceDescriptorSets[frameIndex]) {
    if (!instanceDescriptorSets[frameIndex]) {
      instanceDescriptorSets[frameIndex] =
          context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    }
    DescriptorWriter writer(context);
    vk::DescriptorBufferInfo bInfo =
        compactedInstanceBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(0, bInfo, vk::DescriptorType::eStorageBuffer);

    vk::DescriptorBufferInfo jInfo = jointBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(1, jInfo, vk::DescriptorType::eStorageBuffer);

    writer.updateSet(instanceDescriptorSets[frameIndex]);
  }

  if (updateCullingDescriptor || !cullingDescriptorSets[frameIndex]) {
    if (!cullingDescriptorSets[frameIndex]) {
      cullingDescriptorSets[frameIndex] =
          context.vulkan.globalDescriptorAllocator->allocate(cullingSetLayout);
    }
    DescriptorWriter cWriter(context);
    vk::DescriptorBufferInfo uInfo =
        cullingUboBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo rawInfo =
        instanceBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo compactedInfo =
        compactedInstanceBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo groupFirstInfo =
        groupFirstInstanceBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo groupCountInfo =
        groupVisibleCountBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo cmdGroupInfo =
        commandGroupIndexBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo indirectInfo =
        indirectDrawBuffers[frameIndex]->descriptorInfo();

    cWriter.writeBuffer(0, uInfo, vk::DescriptorType::eUniformBuffer);
    cWriter.writeBuffer(1, rawInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(2, compactedInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(3, groupFirstInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(4, groupCountInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(5, cmdGroupInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(6, indirectInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.updateSet(cullingDescriptorSets[frameIndex]);
  }

  if (instanceBufferSize > 0) {
    instanceBuffers[frameIndex]->map();
    instanceBuffers[frameIndex]->write(cpuInstanceData.data(),
                                       instanceBufferSize);
    instanceBuffers[frameIndex]->unmap();
  }

  if (cmdBufferSize > 0) {
    indirectDrawBuffers[frameIndex]->map();
    indirectDrawBuffers[frameIndex]->write(cpuDrawCommands.data(),
                                           cmdBufferSize);
    indirectDrawBuffers[frameIndex]->unmap();
  }

  if (groupCount > 0) {
    groupFirstInstanceBuffers[frameIndex]->map();
    groupFirstInstanceBuffers[frameIndex]->write(cpuGroupFirstInstance.data(),
                                                 groupCount * sizeof(uint32_t));
    groupFirstInstanceBuffers[frameIndex]->unmap();

    // groupVisibleCountBuffers[frameIndex] is intentionally NOT zeroed
    // here anymore. This function (flushBuffers) only actually runs its
    // body once per frame per frame-in-flight slot (framesDirty-gated),
    // but cull()/cullForViewProj() can each be called MULTIPLE times in
    // the same frame - once per visible lake's reflection pass, plus once
    // more for the real camera (see main.cpp's reflection loop) - all
    // sharing this one buffer. A host map()/write() here would only ever
    // take effect once, before the whole command buffer is submitted -
    // it can't land "in between" two already-recorded compute dispatches
    // on the GPU timeline. Zeroing it here used to silently only reset it
    // before the FIRST cull dispatch of the frame; every subsequent
    // dispatch that frame then atomicAdd'd on top of the previous
    // dispatch's leftover counts instead of starting from zero - which
    // both re-drew already-culled instances a second time (visible as
    // flickering/z-fighting duplicates) and, once a group's accumulated
    // count exceeded that group's own instance slice, overran into and
    // corrupted the NEXT group's region of the compacted buffer. See
    // dispatchCulling()'s vkCmdFillBuffer reset for the actual fix - a
    // real GPU-timeline command, so it resets the counts at the correct
    // point relative to every dispatch recorded into this command buffer,
    // no matter how many times cull()/cullForViewProj() run per frame.
  }

  if (commandCount > 0) {
    commandGroupIndexBuffers[frameIndex]->map();
    commandGroupIndexBuffers[frameIndex]->write(
        cpuCommandGroupIndex.data(), commandCount * sizeof(uint32_t));
    commandGroupIndexBuffers[frameIndex]->unmap();
  }

  if (jointBufferSize > 0 && !cpuJointData.empty()) {
    jointBuffers[frameIndex]->map();
    jointBuffers[frameIndex]->write(cpuJointData.data(),
                                    cpuJointData.size() * sizeof(glm::mat4));
    jointBuffers[frameIndex]->unmap();
  }

  framesDirty[frameIndex] = false;
}

void EntitySys::cull(vk::CommandBuffer cmd) {
  if (sceneBatches.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;
  flushBuffers(frameIndex);

  auto planes = camera::getFrustumPlanes(context.camera.projectionMatrix *
                                         context.camera.viewMatrix);
  CullingUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  ubo.totalCommands = static_cast<uint32_t>(cpuDrawCommands.size());

  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  dispatchCulling(cmd, frameIndex);
}

void EntitySys::cullForViewProj(vk::CommandBuffer cmd,
                                const glm::mat4 &viewProj) {
  if (sceneBatches.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;
  flushBuffers(frameIndex);

  // Only the frustum planes differ from cull() above - totalInstances/
  // totalCommands describe the CURRENT frame's entity set, which is the
  // same regardless of which camera is doing the culling.
  auto planes = camera::getFrustumPlanes(viewProj);
  CullingUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  ubo.totalCommands = static_cast<uint32_t>(cpuDrawCommands.size());

  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  dispatchCulling(cmd, frameIndex);
}

void EntitySys::dispatchCulling(vk::CommandBuffer cmd, int frameIndex) {
  debug::beginLabel(context, cmd, "Culling Dispatch", {.3f, .8f, .3f, 1.f});

  // Reset every group's visible-instance counter to 0 on the GPU timeline,
  // immediately before THIS dispatch's pass 1 - see flushBuffers()'s
  // comment on why this can no longer be a one-time CPU-side write.
  // cull()/cullForViewProj() can each run several times per frame (once
  // per visible lake's reflection, plus once more for the real camera),
  // all sharing this same per-frame-in-flight buffer; each call's pass 1
  // must start its atomicAdd-based compaction from a genuinely fresh
  // zero, or it accumulates on top of whatever the previous dispatch this
  // frame already left behind.
  vk::DeviceSize groupVisibleCountSize =
      groupVisibleCountBuffers[frameIndex]->getSize();
  if (groupVisibleCountSize > 0) {
    // Earlier reads/atomic-writes (this dispatch's own pass 1/2 from a
    // PREVIOUS call this frame, or last frame's, now safely retired via
    // this frame's fences) must finish before the fill overwrites them,
    // and the fill itself must finish before this dispatch's pass 1 reads
    // it again below.
    vk::BufferMemoryBarrier preFillBarrier{};
    preFillBarrier.srcAccessMask =
        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    preFillBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    preFillBarrier.buffer = *groupVisibleCountBuffers[frameIndex];
    preFillBarrier.size = groupVisibleCountSize;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags(), nullptr, preFillBarrier,
                        nullptr);

    cmd.fillBuffer(*groupVisibleCountBuffers[frameIndex], 0,
                   groupVisibleCountSize, 0u);

    vk::BufferMemoryBarrier postFillBarrier{};
    postFillBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    postFillBarrier.dstAccessMask =
        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    postFillBarrier.buffer = *groupVisibleCountBuffers[frameIndex];
    postFillBarrier.size = groupVisibleCountSize;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags(), nullptr, postFillBarrier,
                        nullptr);
  }

  // Pass 2 below overwrites every command's instanceCount outright (not an
  // accumulation), so the CPU-uploaded value doesn't matter - but we still
  // need last frame's indirect-command reads and this frame's host upload
  // to complete before pass 2's shader write can safely land.
  vk::BufferMemoryBarrier indirectBarrier{};
  indirectBarrier.srcAccessMask =
      vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eHostWrite;
  indirectBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
  indirectBarrier.buffer = *indirectDrawBuffers[frameIndex];
  indirectBarrier.size = indirectDrawBuffers[frameIndex]->getSize();

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eDrawIndirect |
                          vk::PipelineStageFlagBits::eHost,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         cullingPipeline->getLayout(), 0, 1,
                         &cullingDescriptorSets[frameIndex], 0, nullptr);

  // Pass 1: frustum-cull instances, compact survivors, accumulate each
  // group's visible count via atomics.
  cullingPipeline->bind(cmd);
  uint32_t instanceGroupCount =
      (static_cast<uint32_t>(cpuInstanceData.size()) + 63) / 64;
  if (instanceGroupCount > 0) {
    cmd.dispatch(instanceGroupCount, 1, 1);
  }

  // Pass 2 reads what pass 1 wrote (compacted buffer + visible counts).
  vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), computeBarrier, nullptr, nullptr);

  // Pass 2: broadcast each group's final visible count into its indirect
  // draw command(s).
  finalizePipeline->bind(cmd);
  uint32_t commandGroupCount =
      (static_cast<uint32_t>(cpuCommandGroupIndex.size()) + 63) / 64;
  if (commandGroupCount > 0) {
    cmd.dispatch(commandGroupCount, 1, 1);
  }

  indirectBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  indirectBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eDrawIndirect,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  debug::endLabel(context, cmd);
}

void EntitySys::render(vk::DescriptorSet globalSetOverride,
                       const glm::vec4 &clipPlane) {
  if (sceneBatches.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  flushBuffers(frameIndex);

  debug::beginLabel(context, cmd, "Indirect Entities", {.7f, .3f, 1.f, 1.f});

  pipeline->bind(cmd);

  cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eVertex, 0,
                    sizeof(glm::vec4), &clipPlane);

  vk::DescriptorSet globalSet =
      globalSetOverride ? globalSetOverride
                        : context.vulkan.globalDescriptorSets[frameIndex];
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 0, 1, &globalSet, 0, nullptr);

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 2, 1,
                         &instanceDescriptorSets[frameIndex], 0, nullptr);

  for (const auto &batch : sceneBatches) {
    batch.scene->bind(context, cmd, *pipeline);

    vk::DescriptorSet texSet = batch.scene->sceneTextureSet;
    if (texSet) {
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             pipeline->getLayout(), 1, 1, &texSet, 0, nullptr);
    }

    cmd.drawIndexedIndirect(
        *indirectDrawBuffers[frameIndex],
        batch.firstDrawCommandOffset * sizeof(vk::DrawIndexedIndirectCommand),
        batch.drawCommandCount, sizeof(vk::DrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}

EntitySys::Entity *EntitySys::pickEntity(const Ray &ray, float &distance,
                                         float maxDistance) {
  Entity *bestEntity = nullptr;
  float minDistance = maxDistance;

  for (auto &entity : entities) {
    if (auto d = ray.intersects(entity.getWorldAABB())) {
      if (*d < minDistance) {
        minDistance = *d;
        bestEntity = &entity;
      }
    }
  }

  if (bestEntity) {
    distance = minDistance;
  }
  return bestEntity;
}

EntitySys::Entity *EntitySys::getPointingAt(float maxDistance) {
  Ray ray = camera::getPickingRay(context, glm::vec2(0.0f));
  float distance;
  return pickEntity(ray, distance, maxDistance);
}

} // namespace vkh
