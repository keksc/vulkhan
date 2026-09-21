#version 450

layout(vertices = 4) out;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
} vertexUBO;

layout(location = 0) in vec3 inPos[];
layout(location = 1) in vec2 inUV[];

layout(location = 0) out vec3 outPosTCS[];
layout(location = 1) out vec2 outUVTCS[];

void main() {
  outPosTCS[gl_InvocationID] = inPos[gl_InvocationID];
  outUVTCS[gl_InvocationID] = inUV[gl_InvocationID];

  if (gl_InvocationID == 0) {
    vec3 worldPos[4];
    for (int i = 0; i < 4; ++i) {
        worldPos[i] = (vertexUBO.model * vec4(inPos[i], 1.0)).xyz;
    }
    vec3 patchCenter = (worldPos[0] + worldPos[1] + worldPos[2] + worldPos[3]) / 4.0;
    vec3 camPos = vec3(ubo.inverseView[3]);
    float distance = length(patchCenter - camPos);

    // Distance thresholds scaled to THIS lake's own world size, not fixed
    // absolute units. The old formula (`256.0 - distance * 0.2`, clamped
    // 1..16) only actually starts dropping below the hard max at
    // distance == 1200, and doesn't reach the minimum until ~1275 - a
    // razor-thin falloff band, tuned for the since-removed camera-
    // following open-ocean tile (see WaterCreateInfo's doc comment: "no
    // camera-follow mode anymore"), which spanned thousands of world
    // units. A lake patch is tens to a few hundred world units across -
    // every on-screen quad of an actual lake sat well inside that old
    // "close" range, so gl_TessLevel* was pinned to the hard max (16)
    // across the ENTIRE visible surface, everywhere, regardless of
    // distance - i.e. every one of this mesh's 32x32 base quads (see
    // WaterSys::tileResolution) was ALSO subdivided a further 16x16,
    // simultaneously, everywhere on screen. That's an extremely fine,
    // perfectly regular geometric grid - fine when real wave displacement
    // (WaterCreateInfo::waveScale / global wind) is high enough to break
    // up that regularity, but with it flattened out (calm water, low
    // wind) nothing does, and it aliases into a moiré/checkerboard
    // pattern.
    //
    // patchWorldWidth mirrors water.tese's own use of
    // vertexUBO.model[0]'s length (see its worldTexelDist comment) - this
    // is the WHOLE lake patch's world-space width (WaterCreateInfo::size
    // .x), not a single grid cell's. Using it (rather than an absolute
    // meter figure) means these thresholds automatically scale correctly
    // for any lake size without per-lake tuning: full extra tessellation
    // only within a fraction of this lake's own width, fully coarsened by
    // roughly one lake-width away.
    float patchWorldWidth = length(vec3(vertexUBO.model[0]));
    // nearDist widened 0.15 -> 0.25 of the patch width - keeps the
    // maximum tessellation level active over a bit more of the space
    // right around the camera, rather than starting to coarsen almost
    // immediately.
    float nearDist = patchWorldWidth * 0.25;
    float farDist = patchWorldWidth * 1.0;
    // smoothstep rather than a hard linear ramp - reduces (though, with
    // each patch still computing its tessLevel independently, doesn't
    // fully eliminate - a real fix needs edge-matched tessellation levels
    // shared between neighboring patches) visible seams/cracks at patch
    // boundaries that straddle the falloff band, since a gentler curve
    // means adjacent patches a similar distance from the camera are more
    // likely to land on the same or an adjacent integer tessellation
    // level rather than jumping across a steep step.
    //
    // Cap raised 16.0 -> 24.0 - even a calmer wave amplitude (see
    // WaterCreateInfo::waveScale's comment in main.cpp) still has locally
    // steep slopes, and 16 subdivisions per base quad wasn't quite enough
    // to keep those looking like a smooth curved surface rather than
    // visible polygon facets/creases at close range. This is a real
    // per-vertex/per-triangle cost increase at close range - if it costs
    // too much on lower-end hardware, prefer pulling waveScale down
    // further over pushing this higher still, since amplitude is the
    // thing actually driving how much geometric detail is needed here.
    float tessLevel = mix(24.0, 1.0, smoothstep(nearDist, farDist, distance));

    gl_TessLevelOuter[0] = tessLevel;
    gl_TessLevelOuter[1] = tessLevel;
    gl_TessLevelOuter[2] = tessLevel;
    gl_TessLevelOuter[3] = tessLevel;
    gl_TessLevelInner[0] = tessLevel;
    gl_TessLevelInner[1] = tessLevel;
  }
}
