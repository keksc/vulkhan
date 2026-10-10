# Application compilation boundaries

`src/main.cpp` is now a 20-line platform entry point and exception boundary.
It includes only `app/application.hpp` and standard/platform headers, and skips
the target's Vulkan/glTF precompiled header. Engine header changes therefore do
not recompile the entry point.

Application code is split into these separately compiled files:

| File | Responsibility |
| --- | --- |
| app/application.cpp | Startup, gameplay/streaming orchestration and frame updates |
| app/assets.cpp | Procedural tree/rock assets and postprocessing factories |
| app/lakes.cpp | Lake streaming, recycling, exclusions, refine zones and visibility |
| app/renderFrame.cpp | Reflections, waves, scene passes, postprocessing, HUD and submission |
| app/math.cpp | Frustum helpers and attractor generation |

LakeStreaming hides async jobs, caches and retirement state behind a private
implementation. Destruction still waits for lake analysis futures while the
referenced mountain parameters and engine resources remain alive. Scene pass
ordering is preserved: reflected culling/rendering precede main culling;
wave compute occurs outside rendering scopes; HUD runs after postprocessing.
The initial lake refine zones are installed before terrain prewarm.

Procedural generators and compute-effect implementation headers are isolated
in assets.cpp. Orchestration loads its bird asset through EntitySys's loader,
so it no longer includes sceneBuilder.hpp. CMake source discovery uses
CONFIGURE_DEPENDS to notice added/removed .cpp files automatically.

The orchestration file still includes systems needed for frame updates; shared
header changes can rebuild it. This change narrows feature implementation edits,
rather than eliminating the normal dependencies of shared interfaces.

Verification: build in Debug, run tests/appMath.cpp with app/math.cpp, and inspect
main.cpp's dependency file/compile command to confirm no engine/PCH dependency.
After a successful build, touch app/assets.cpp and rebuild: only that object
and linking should run, with main/application object timestamps unchanged.
Runtime rendering verification requires a GPU, unavailable in this environment.

Verified in this workspace: Debug build passed, extracted math tests passed,
entry-point dependency inspection found no engine/PCH headers, and an actual
asset-only rebuild changed only assets.cpp.o plus the linked executable.
