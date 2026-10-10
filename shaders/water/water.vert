#version 450

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
    vec2 uvRepeat;
    float time;
    float chopScale;
    float shoreFadeDepth;
    float cellWorld;
    mat4 reflViewProj;
} vertexUBO;

layout(set = 1, binding = 1) uniform sampler2D displacementFoamMap;

#include "waveSampling.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
// Water depth under this vertex, 0 on the shore (WaterCreateInfo::meshDepth).
layout(location = 2) in float inShoreDepth;

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outReflClipPos;
layout(location = 4) out float outWaterDepth;

vec3 waveDisplacement(vec4 sampleValue,float fade) {
    vec2 horizontal=sampleValue.xz*vertexUBO.chopScale;
    float maxChop=vertexUBO.cellWorld*0.3;
    horizontal*=min(1.0,maxChop/max(length(horizontal),1e-5));
    return vec3(horizontal.x,clamp(sampleValue.y*vertexUBO.scale,-0.55,0.55),horizontal.y)*fade;
}

void main() {
    // No tessellation any more: the lake mesh is a fine lattice with the shore
    // vertices already on the real shoreline (see WaterCreateInfo::meshXZ), so
    // each vertex is processed here directly.
    outWaterDepth = max(inShoreDepth, 0.0);
    vec3 pos = inPos;
    vec2 baseUV = inUV;

    // Wave displacement dies out toward the shore, so the surface meets the
    // terrain exactly instead of waving through it or lifting off it.
    float shoreFade = smoothstep(0.0, max(vertexUBO.shoreFadeDepth, 1e-4), inShoreDepth);

    vec4 worldPosBase = vertexUBO.model * vec4(pos, 1.0);

    // Tile the wave/foam texture across this patch's own local UV space
    // (baseUV is 0..1 across the whole quad, independent of world scale)
    // rather than off absolute world position - a fixed world-space UV
    // divisor doesn't know how big any individual lake patch is, so a
    // small lake barely moved across one texture tile at all before this
    // and looked almost static/un-tiled. uvRepeat is set per-instance in
    // WaterCreateInfo (see WaterSys/main.cpp) specifically to control how
    // many times the pattern repeats across THIS patch.
    vec2 texCoord = baseUV * vertexUBO.uvRepeat;

    ivec2 texSize = textureSize(displacementFoamMap, 0);
    vec2 texelSize = 1.0 / vec2(texSize);

    // worldTexelDist: world-space distance spanned by one mip-0 texel.
    // One full texture width (texCoord 0..1) is traversed over baseUV
    // going from 0 to 1/uvRepeat.x, i.e. over world distance
    // (patch width) / uvRepeat.x - so a texel spans that divided by the
    // texture's pixel width. Needed here already since the LOD
    // calculation below depends on it. Guarded against
    // vertexUBO.uvRepeat.x == 0 (e.g. a degenerate, zero-width lake
    // bounding box from mountain/lake generation) - an unguarded division
    // here produces Inf, which propagates into displacedPos/gl_Position
    // below and rasterizes as wildly out-of-place, radiating shard-like
    // triangles (a classic NaN/Inf vertex-position symptom) instead of
    // failing visibly/safely.
    float patchWorldWidth = length(vec3(vertexUBO.model[0]));
    float worldTexelDist =
        patchWorldWidth / max(float(texSize.x) * vertexUBO.uvRepeat.x, 1e-5);

    // Explicit LOD for the displacement/normal reads below, instead of
    // implicit-LOD texture() (which is what this used to call). texture()
    // only has valid automatic derivatives in a FRAGMENT shader - here,
    // in the tessellation evaluation stage, there is no such thing as a
    // screen-space derivative, so an implicit-LOD call is undefined by
    // the GLSL spec and every driver that accepts it anyway just pins it
    // to mip 0, always, regardless of distance or tessellation density.
    // That silently wastes the mip chain WaterSys::createSampler()/
    // WSTessendorf::recordGenerateMips() actually built.
    //
    // In practice this mattered even in the NEAR field at max
    // tessellation: uvRepeat (see WaterCreateInfo) can tile the
    // displacement texture many times across a lake, so mip-0 texels can
    // still be substantially smaller than the world-space gap between
    // adjacent TESSELLATED VERTICES. Reading raw, unfiltered mip-0 noise
    // at that gap means neighboring vertices sample essentially
    // uncorrelated points of the height field - which is exactly what
    // produced the sharp, flat-faceted "crystalline" look (interpolation
    // is smooth WITHIN each triangle, but adjacent triangles' vertices
    // disagree wildly, so triangle edges show as visible facet
    // boundaries), and, on faces where the resulting normal happens to
    // angle away from the reflection, let the plain base water color
    // (not a real absence of nearby blue objects - see water.frag's
    // waterColor) show through in patches that read as "reflecting a
    // color that isn't in the scene".
    //
    // The vertex spacing below is the mesh's lattice cell size. Comparing it
    // with worldTexelDist gives how many mip-0 texels apart adjacent vertices
    // sample; log2 of that ratio is the mip level at which one texel matches
    // one vertex - the standard "match sample density to source resolution"
    // LOD, done by hand since this stage has no implicit derivatives.
    // Vertex spacing is the lattice cell size the lake mesh was built with
    // (WaterCreateInfo::meshCellSize), passed in the UBO.
    float vertexSpacingWorld = vertexUBO.cellWorld;
    float vertexSpacingTexels = vertexSpacingWorld / max(worldTexelDist, 1e-8);
    // Clamped to a conservative ceiling, NOT left to grow unbounded with
    // distance. The raw (unclamped) value matches mip level to vertex
    // spacing exactly - but water.tesc's own distance falloff ALREADY
    // coarsens vertex spacing a lot at range (down to 1/16th of near-
    // field density), so this would otherwise stack a second, compounding
    // round of coarsening on top of that. A wave-height field oscillates
    // around zero, so a large enough box-filtered mip washes out not just
    // fine texel-level noise but the genuine LARGE-SCALE wave shape too -
    // averaging over enough wavelengths of even the big waves drives the
    // result toward flat. The ceiling below still reaches far enough to
    // fix the texel-level aliasing that caused the faceted "crystalline"
    // look, without being able to flatten real wave motion in the
    // process. If the water still reads as flatter than expected after
    // this, that's a wave AMPLITUDE tuning question (WaterCreateInfo::
    // waveScale / global wind, see water.hpp) rather than a sampling bug
    // - a low enough waveScale genuinely produces subtle real
    // displacement, and the old unfiltered aliasing was likely making it
    // look busier/more energetic than the simulation actually was.
    float lod = min(log2(max(vertexSpacingTexels, 1.0)), 3.0);

    // Scale the finite-difference tap offset to match whichever mip `lod`
    // lands on (2^lod mip-0 texels per step there) - sampling immediately
    // -adjacent mip-0-sized texels from an already-blurred coarse mip
    // would just fetch nearly-identical values (since a coarse mip varies
    // slowly), understating the true slope and making distant water look
    // artificially flat. Scaling the tap distance keeps the derived
    // normal's magnitude consistent with the actual vertex spacing
    // regardless of which mip is in play.
    vec2 offset = texelSize * exp2(lod);

    // Differentiate the same composite field used for displacement and foam.
    vec4 d_center = sampleWaterWaves(texCoord, lod);
    vec4 d_right = sampleWaterWaves(texCoord + vec2(offset.x, 0.0), lod);
    vec4 d_left = sampleWaterWaves(texCoord - vec2(offset.x, 0.0), lod);
    vec4 d_top = sampleWaterWaves(texCoord + vec2(0.0, offset.y), lod);
    vec4 d_bot = sampleWaterWaves(texCoord - vec2(0.0, offset.y), lod);

    // vertexUBO.scale (WaterCreateInfo::waveScale) damps height, and
    // vertexUBO.chopScale (WaterCreateInfo::chopScale) independently damps
    // horizontal (XZ) displacement - Tessendorf's "chop" term, which pulls
    // vertices sideways to sharpen wave crests. These used to share one
    // scalar over the whole displacement vector, which meant raising
    // waveScale for taller waves ALSO raised chop, and past a certain
    // point (chop displacement exceeding roughly half the grid's vertex
    // spacing) neighboring vertices overtake each other in XZ and the
    // mesh folds over itself - see WaterCreateInfo::chopScale's full
    // comment. Splitting them lets waveScale go arbitrarily high for wave
    // height without ever re-triggering that.
    //
    // Scaling the neighbor-sample deltas the same way as the center
    // sample keeps the derived normal consistent with the actually-
    // displayed (damped) surface instead of computing a normal for
    // full-strength waves that aren't there.
    vec3 dispCenter = waveDisplacement(d_center,shoreFade);
    vec3 dispRight = waveDisplacement(d_right,shoreFade);
    vec3 dispLeft = waveDisplacement(d_left,shoreFade);
    vec3 dispTop = waveDisplacement(d_top,shoreFade);
    vec3 dispBot = waveDisplacement(d_bot,shoreFade);

    // Tangent/bitangent base spacing must match the ACTUAL world-space
    // distance between the two tap positions used for dispRight/dispLeft
    // and dispTop/dispBot above - now offset (texelSize * exp2(lod)) mip-0
    // texels apart, not one bare texelSize apart, since the taps moved
    // when the LOD calculation above started scaling them. Using the old
    // fixed worldTexelDist spacing here while the actual taps had already
    // moved further apart would understate the base (non-displaced) term
    // relative to the real gap, skewing the derived normal.
    vec3 tangent   = vec3(2.0 * worldTexelDist * exp2(lod), 0.0, 0.0) + (dispRight - dispLeft);
    vec3 bitangent = vec3(0.0, 0.0, 2.0 * worldTexelDist * exp2(lod)) + (dispTop - dispBot);

    outUV = texCoord;

    // Guarded against a near-zero-length cross product (e.g. from very
    // calm/low-amplitude water where dispRight/dispLeft or dispTop/dispBot
    // land close enough to identical for floating-point cancellation) -
    // normalize() of a zero vector is also NaN, with the same
    // shattered-geometry symptom as the division above. Falls back to
    // world-up, a reasonable default for an (almost) perfectly flat patch.
    vec3 crossVec = cross(bitangent, tangent);
    vec3 normal = length(crossVec) > 1e-8 ? normalize(crossVec) : vec3(0.0, 1.0, 0.0);

    // Apply (scaled) displacement to world position
    vec3 displacedPos = worldPosBase.xyz + dispCenter;

    outPos = vec4(displacedPos, d_center.w);
    outNormal = normal;

    // Reflect the true view ray around THIS vertex's actual (wave-tilted)
    // normal, rather than reprojecting displacedPos itself through the
    // fixed mirrored camera unmodified - the old approach ignored `normal`
    // entirely for outReflClipPos (water.frag's small N.xz*0.02 UV nudge
    // was the only place waviness ever reached the reflection at all),
    // which is why the reflection read as a flat mirror regardless of
    // wave height: two vertices with identical position but wildly
    // different tilt reprojected identically.
    //
    // PlanarReflectionSys's mirrored camera only ever captured ONE flat
    // plane's worth of the world (see its own class doc comment) - there
    // is no way to recover a true per-normal reflection from a single
    // fixed-viewpoint render, so this is still an approximation, just a
    // much better one: it finds where a properly-reflected ray would
    // cross that same flat mirror plane, and samples the texture there,
    // instead of sampling at the vertex's own (un-reflected) position.
    // camPos is the REAL camera's position (not the mirrored one) -
    // ubo.projView/ubo.inverseView here are the real camera's own,
    // exactly like water.frag's identical camPos derivation.
    vec3 camPos = vec3(ubo.inverseView[3]);
    vec3 V = normalize(camPos - displacedPos);
    vec3 R = reflect(-V, normal);

    // planeY: worldPosBase.y is exactly the flat, pre-displacement grid
    // height - always equal to WaterCreateInfo::center.y (the water
    // level), since this patch's base mesh has y == 0 everywhere in local
    // space and vertexUBO.model's translation is center.y - so this needs
    // no separate uniform, it falls out of what's already computed above.
    float planeY = worldPosBase.y;
    float denom = R.y;
    float rawT = (planeY - displacedPos.y) / denom;
    // Guard near-parallel-to-plane / away-from-plane rays (denom ~ 0, or a
    // negative t meaning the reflected ray points away from the plane
    // rather than toward it) - both make "where does R cross y==planeY"
    // either undefined or nonsensical (a point behind the ray's origin).
    //
    // ALSO capped at maxT (this patch's own world-space width, already
    // computed above for the LOD calculation) - PlanarReflectionSys's
    // mirrored camera only ever captured a bounded region around THIS
    // lake (see its class doc comment), so an intersection point many
    // patch-widths away is guaranteed to land somewhere outside anything
    // the reflection target actually rendered, regardless of whether the
    // math to get there is "correct". Without this cap, a reflected ray
    // that's merely CLOSE to parallel with the plane (not close enough to
    // trip the denom guard, but still small) produces a huge t - pushing
    // reflSamplePos far outside the mirrored camera's frustum, which
    // water.frag's validReflW fade then zeroes out. At certain camera
    // heights/pitches this happens across most of the visible surface at
    // once (R.y depends mostly on view angle, which is shared across
    // nearby fragments), which is what read as the WHOLE reflection
    // suddenly going flat/blue rather than a few isolated bad pixels.
    //
    // Falls back to the OLD raw-position reprojection whenever the
    // corrected point wouldn't be trustworthy anyway - same spirit as
    // this shader's other NaN/degenerate guards above: a slightly-less-
    // accurate-but-sane UV beats a garbage or out-of-frustum one.
    float maxT = patchWorldWidth;

    // Fade the ray-based correction out as the view approaches straight
    // down (V nearly vertical) - this is the regime where it actually
    // gets WORSE, not just unnecessary. Near-nadir views make the
    // reflected ray R nearly vertical too, which means intersecting it
    // against the (horizontal) mirror plane is close to a degenerate
    // near-parallel-ray-vs-plane situation in the OTHER sense from the
    // denom guard below: R.y stays comfortably away from zero, but the
    // actual INTERSECTION POINT becomes highly sensitive to small changes
    // in R's direction - and R's direction is driven by the local wave
    // normal, which at real wave amplitudes (see WaterCreateInfo::
    // waveScale) tilts by a meaningful amount crest to crest. The result:
    // technically-valid intersection points that land on genuinely
    // different, unrelated parts of the reflection target than what
    // should actually be reflected there - which reads as a ghosted/wrong
    // second reflection rather than the expected one, concentrated
    // exactly where the view is most vertical (dead center of a
    // straight-down view). The plain reprojection below has no such
    // sensitivity (it doesn't route through R at all), and is visually
    // adequate for near-nadir views regardless - tilt correction matters
    // most at oblique/grazing angles, which is the opposite of this case.
    float viewVerticality = abs(V.y);
    float rayFade = 1.0 - smoothstep(0.85, 0.98, viewVerticality);

    // Fade the correction out as the reflected ray approaches PARALLEL to
    // the mirror plane (denom == R.y -> 0), instead of the previous hard
    // `abs(denom) > 1e-5` cutoff. This is NOT a rare grazing-angle edge
    // case: for near-flat water (normal ~= (0,1,0)), R.y works out to
    // exactly V.y (reflect(-V, (0,1,0)) only flips the horizontal
    // components) - i.e. denom passes through zero at ordinary,
    // everyday near-horizontal eye-level views of the lake, not just at
    // extreme angles. Right up against the old 1e-5 threshold, rawT below
    // could still take on huge (but "valid") values, and since near-flat
    // water also makes the numerator (planeY - displacedPos.y) small,
    // rawT there is a highly noise-sensitive ~0/~0 ratio - the smallest
    // per-fragment, per-frame wave-normal jitter (present even at a calm
    // waveScale) swings it unpredictably. That instability, not an
    // actual out-of-bounds sample (reflUV is always clamped below), is
    // what read as the reflection flickering/jumping on calm water: with
    // large waves the normal tilt dominates and swamps this jitter, which
    // is why the flicker only showed up once the water was made flatter.
    float denomFade = smoothstep(0.0, 0.02, abs(denom));

    // Fade out approaching the maxT cap too, rather than hard-cutting the
    // instant rawT crosses it - same reasoning: a boolean flip pops
    // visibly every time a wave normal nudges rawT across the boundary,
    // a fade doesn't.
    float capFade = 1.0 - smoothstep(maxT * 0.7, maxT, rawT);

    bool rawTPositive = rawT > 0.0;
    float correctionWeight =
        rawTPositive ? (denomFade * capFade * rayFade) : 0.0;

    // Clamp rawT defensively before using it - denomFade already zeroes
    // its contribution near the denom==0 singularity, but rawT itself can
    // still be a huge finite value there, and multiplying a huge value by
    // a fade approaching 0 right at that boundary is exactly the kind of
    // thing float precision handles badly.
    float safeRawT = min(rawT, maxT);

    vec3 reflSamplePos = displacedPos + R * safeRawT * correctionWeight;

    outReflClipPos = vertexUBO.reflViewProj * vec4(reflSamplePos, 1.0);

    gl_Position = ubo.projView * vec4(displacedPos, 1.0);
}
