# Using EntitySys

Create one model instance with one stable handle. The system handles mesh
expansion, scene upload, batching invalidation and removal bookkeeping.

```cpp
auto model = entitySys.loadScene("models/MaterialsVariantsShoe.glb");
vkh::EntitySys::Transform transform;
transform.position = {12.f, 3.f, 8.f};
auto avatar = entitySys.spawn(model, transform);

transform.position.x += 2.f;
entitySys.setTransform(avatar, transform);
entitySys.setColor(avatar, {1.f, .7f, .5f, 1.f});
entitySys.setId(avatar, networkPlayerId); // optional gameplay identity

entitySys.destroy(avatar); // removes every mesh of the model
assert(!entitySys.isAlive(avatar));
```

For a single model, `entitySys.spawn(path, transform)` also loads/uploads it.
Loading through `loadScene` caches live assets by normalized absolute path and
material mode. Repeated instances share GPU geometry and textures. Cache
references are weak so unused assets can retire. Loading/upload runs on the
main/render thread and can block; preload outside the frame loop. Existing
SceneBuilder supports background CPU parsing followed by main-thread GPU upload.

A handle survives vector growth and other instances being removed. Deleted or
foreign handles fail safely: setters/destroy return false, getTransform returns
nullopt. Reusing a slot increments its generation. `getTransform(handle)` returns
a copy; change it and call setTransform. `pickInstance(ray, distance, maxDistance)`
returns the whole managed instance when any of its rendered mesh bounds is hit.
Legacy mesh-level entities are skipped by this picking API.

## Frame integration

After `beginFrame` has retired the current frame fence and set frameInfo:

```cpp
entitySys.prepareFrame(); // CPU instances/joints + retired-slot GPU uploads
entitySys.cull(commandBuffer); // outside an active rendering pass
// Begin the scene rendering pass.
entitySys.render();
```

Prepare once per frame after gameplay edits. Existing updateBuffers/updateJoints
calls remain available, but prepareFrame combines them. Reflection culling and
rendering still require the existing ordering: render mirrored views first,
then cull for the main view. Compute culling must stay outside rendering passes.

The gameplay entity vector stays in its own order. A cached sorted index list
batches by shared scene and mesh, rebuilt only when structure changes. This
combines interleaved model meshes without moving identities. Multi-mesh models
spawn every renderable mesh; LOD assets spawn one representative so the existing
LOD expansion draws each level's parts once. Skinned joint data is uploaded in
draw-group order, without the old second entity-order overwrite.

Frame-owned scene references keep resources alive while GPU commands use them;
the retired frame slot refreshes its references when uploaded. No extra global
GPU wait is introduced on despawn.

## Compatibility and Scene

Existing mesh-level entities remain supported. Do not reorder, erase or change
ownerSlot for entries created by the managed API; use destroy. Held vector
indices and references are legacy interfaces and can be invalidated by removals
or vector growth. Existing fixed boid indices remain before managed avatars;
multiplayer avatars now use stable handles instead of swap-index repair loops.

Scene exposes isUploaded() and instanceMeshes(). CPU-only scenes passed to spawn
are uploaded automatically. SceneBuilder's construction now uses the correct
path constructor and parses once. Animation poses remain on shared Scene assets:
updateAnimation changes all instances sharing that scene. Independent poses are
not provided by this API change.

Validation: Debug build, tests/entityHandles.cpp and tests/entityBatching.cpp.
Handle tests exercise growth, cross-registry ownership, stale handles and slot
reuse. Batch tests reduce 300 interleaved entries to three groups while preserving
entity identities. Runtime GPU verification needs the target machine: spawn
multiple multi-mesh assets, move/tint/pick them, remove a middle instance, reuse
its slot, and test skinned/LOD assets and reflections with Vulkan validation.
