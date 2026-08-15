#version 450

#include "../globalUbo.glsl"
#include "../consts.glsl"

layout (location = 0) out vec4 outColor;

layout (set = 1, binding = 0) uniform samplerCube milkyWayCubemap;
layout (set = 1, binding = 1) uniform sampler2D discTurbulence;

const float FOV_Y = 1.919862177;

const float DAY_LENGTH_SECONDS = 60.0;

// reconstruct a world-space view ray from the fragment's screen position
vec3 getWorldRayDir()
{
  vec2 ndc = (gl_FragCoord.xy / ubo.resolution) * 2.0 - 1.0;
  ndc.y = -ndc.y; // flip for Vulkan's top-left origin / y-down NDC

  float tanHalfFov = tan(FOV_Y * 0.5);
  vec3 viewDir = normalize(vec3(ndc.x * tanHalfFov * ubo.aspectRatio,
                                 ndc.y * tanHalfFov,
                                 -1.0));

  vec3 worldDir = mat3(ubo.inverseView) * viewDir;
  return normalize(worldDir);
}

// =====================================================================
// inspired by:
//     https://www.shadertoy.com/view/tXsGR2
//
// The gravitational lensing raymarch below is real-time
//
// The artistic contrast curve (pow(color, 1.7)) is applied only to the
// black hole's own light contribution (glow/disc/jets), not to the whole
// sky, so it doesn't also darken the background stars.
// =====================================================================

#define BH_EPSILON 0.1

struct BHSphere {
  float radius;
  float mass;
};
struct BHTorus {
  float minor;
  float major;
};

const BHSphere bhSphere = BHSphere(
  0.125,
  2.5 * 0.001 // premultiplied G
);
const BHTorus bhTorus = BHTorus(
  0.8,
  0.99
);

// How far away the black hole orbits in real world units, and how fast.
const float BH_ORBIT_RADIUS = 500.0;
const float BH_ORBIT_PERIOD_SECONDS = 120.0;

// Local raymarch units per world unit, auto-derived from BH_ORBIT_RADIUS.
// The SDF scene (sphere radius 0.125, disc out to ~1.0, step 0.02) was
// tuned assuming an eye a couple of local units away when standing near
// the world origin - BH_CANONICAL_EYE_DIST is that reference distance, so
// this scales world offsets down consistently no matter what
// BH_ORBIT_RADIUS is set to.
const float BH_CANONICAL_EYE_DIST = 2.0;
const float BH_WORLD_TO_LOCAL = BH_CANONICAL_EYE_DIST / BH_ORBIT_RADIUS;

// Where the black hole actually sits in the world right now, so it can be
// orbited and parallaxed against as the camera moves - unlike the rest of
// this sky pass, which is direction-only / infinitely far away.
vec3 bhWorldPos() {
  float angle = ubo.time / BH_ORBIT_PERIOD_SECONDS * TWO_PI;
  return vec3(cos(angle), 0.0, sin(angle)) * BH_ORBIT_RADIUS;
}

float sdfSphere(in vec3 p, in BHSphere s) {
  return length(p) - s.radius;
}
float sdfTorus(in vec3 p, in BHTorus t) {
  vec2 q = vec2(length(p.xz) - t.minor, p.y);
  return length(q) - t.major;
}

// Local eye distance beyond which the view of the black hole's diorama
// stops changing - i.e. once the camera is farther than
// BH_MAX_EYE_DIST / BH_WORLD_TO_LOCAL world units away, the black hole
// keeps looking the same (like a distant sky object) instead of shrinking
// out of view. This matches BH_CANONICAL_EYE_DIST, the distance the SDF
// scene (sphere radius 0.125, disc out to ~1.0) was tuned to be viewed
// from. Below this distance, eye is real/unclamped, so parallax works
// normally as the camera approaches.
const float BH_MAX_EYE_DIST = BH_CANONICAL_EYE_DIST;
// Width of the smooth transition into the BH_MAX_EYE_DIST clamp - wider
// means the clamp "wakes up" more gradually as offsetDist approaches
// BH_MAX_EYE_DIST, instead of hard-cornering the instant it's crossed.
const float BH_EYE_SOFTEN = 0.6;

// Polynomial smooth minimum (Inigo Quilez) - like min(a, b) but blends
// smoothly across a band of width k around where a and b cross, instead
// of a hard corner. Used below to clamp offsetDist to BH_MAX_EYE_DIST
// without a visible snap in how quickly parallax ramps in.
float smin(float a, float b, float k) {
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

// Gravitational-lensing raymarch, ported from the original "mainImage".
// `rayDir` is the world-space view ray. Returns true if the ray was
// captured by the event horizon (in which case the background should be
// black at that point), always fills in the accumulated glow/disc/jets
// light so it can be added on top of the background regardless of
// capture, and always fills in `outRayDir` with the ray's final
// (gravitationally bent) direction - the background must be sampled with
// *this*, not the original `rayDir`, for the black hole to actually
// distort/lens the sky behind it rather than just glowing on top of an
// undistorted background.
bool raymarchBlackHole(in vec3 rayDir, out vec3 glow, out vec3 disc,
                       out vec3 jets, out vec3 outRayDir) {
  // Local "eye" for marching the black hole's own small-scale geometry,
  // derived from the *real* camera position relative to the black hole's
  // current world position - this is what gives the effect real parallax
  // as the player approaches. World-space camera position is the
  // translation column of inverseView (view -> world); the offset is
  // compressed into the raymarch's local unit space via
  // BH_WORLD_TO_LOCAL, then its *distance* (not direction) is smoothly
  // clamped to BH_MAX_EYE_DIST via smin() - so up close the eye moves
  // (almost) freely for real parallax, and past that distance it
  // gradually stops receding and just holds its direction, keeping the
  // black hole visible/stable from arbitrarily far away instead of fading
  // out once it's outside the local diorama. The smooth clamp (vs a hard
  // min) avoids a visible snap in how quickly parallax ramps in/out
  // around BH_MAX_EYE_DIST.
  vec3 cameraWorldPos = ubo.inverseView[3].xyz;
  vec3 offset = (cameraWorldPos - bhWorldPos()) * BH_WORLD_TO_LOCAL;
  float offsetDist = length(offset);
  float clampedDist = smin(offsetDist, BH_MAX_EYE_DIST, BH_EYE_SOFTEN);
  vec3 eye = offset * (clampedDist / max(offsetDist, 0.0001));

  glow = vec3(0.0);
  disc = vec3(0.0);
  jets = vec3(0.0);
  outRayDir = rayDir;

  // Early-out: closest approach of the (undeflected) ray to the black
  // hole, computed analytically instead of by marching. Safe as a hard
  // cutoff (no fade needed) because eye's distance is now clamped to
  // BH_MAX_EYE_DIST above - unlike before clamping was added, this
  // boundary no longer shifts with camera position, so there's nothing to
  // pop.
  float tClosest = max(dot(-eye, rayDir), 0.0);
  float closestDist = length(eye + tClosest * rayDir);
  if (closestDist > BH_MAX_EYE_DIST + 0.5) {
    return false;
  }

  vec3 rd = rayDir;
  vec3 p = eye;

  bool captured = false;
  float noncaptured = 1.0;

  // Squared version of the early-out radius, reused inside the loop below
  // to detect "ray has passed the black hole and is now receding past the
  // point where anything else it does matters" - avoids burning the rest
  // of the iteration budget on rays that grazed past instead of heading
  // toward the horizon or disc.
  float bhExitRadiusSq = (BH_MAX_EYE_DIST + 0.5) * (BH_MAX_EYE_DIST + 0.5);

  for (float t = 0.0; t < 1.0; t += 0.005) {
    p += rd * 0.02 * noncaptured;

    vec3 to = - p;
    float r2 = dot(to, to);
    rd += normalize(to) * (bhSphere.mass / r2);

    float s = sdfSphere(p, bhSphere);
    noncaptured = smoothstep(0.0, 0.666, s);

    glow += vec3(1.0, 0.9, 0.85) * (1.0 / r2) * 0.0030 * noncaptured;

    // The disc's final contribution below is gated by
    // smoothstep(0.0, 1.0, -torusDist), which is exactly zero whenever
    // torusDist >= 0.0 (outside the torus). Checking that up front and
    // skipping the noise-texture sample + surrounding shading math for
    // the (large majority of) steps that aren't near the disc is a free
    // win - it changes zero pixels, since those steps contributed nothing
    // to `disc` either way.
    float torusDist = sdfTorus(p * vec3(1.0, 25.0, 1.0), bhTorus);
    if (torusDist < 0.0) {
      float discRadius = length(to.xz);
      float discAngle = atan(to.x, to.z);

      // discAngle jumps by -2*PI at its branch cut (the atan wraparound).
      // The V coordinate below is discAngle * discFreq * 10.0 (after the
      // vec2(10,20) and vec2(0.1,0.5) scales combine to a factor of 10),
      // so that jump lands on the texture sampler's wrap boundary as
      // -2*PI * discFreq * 10.0 texture cycles. If that isn't an exact
      // integer, the sampled noise doesn't line up across the branch cut
      // and shows up as a hard seam - visible as a straight line through
      // the disc since discFreq varies continuously with radius and is
      // essentially never an integer number of cycles by chance. Snapping
      // discFreq to the nearest value that *does* land on an integer
      // fixes this exactly, at a correction small enough (a fraction of a
      // percent) not to visibly change the shear/swirl look.
      float discFreqRaw = 0.01 + (discRadius - bhSphere.radius) * 0.002;
      float discCycles = round(discFreqRaw * 20.0 * PI);
      float discFreq = discCycles / (20.0 * PI);

      vec2 discSample = vec2(
          discRadius,
          discAngle * discFreq +
          2.0 * PI + -ubo.time * 0.005
      ) * vec2(10.0, 20.0);

      float discNoise = texture(discTurbulence, discSample * vec2(0.1, 0.5)).r;
      float discMask = max(0.0, discNoise + 0.05) * (4.0 / (0.001 + s * 50.0));

      vec3 discColor = mix(
          vec3(1.0, 0.8, 0.6),
          vec3(0.5, 0.46, 0.4),
          s * s
      ) * discMask;

      disc += max(
          vec3(0.0),
          discColor * smoothstep(0.0, 1.0, -torusDist) * noncaptured
      );

      jets += vec3(0.5, 0.7, 0.99) * (1.0 / discRadius) * noncaptured * 0.0001;
    }

    if (s < BH_EPSILON) {
      captured = true;
      break;
    }

    // Ray has passed the black hole and is now moving away, already
    // farther out than anything else in this function cares about -
    // nothing left to accumulate, so stop marching instead of running out
    // the rest of the fixed iteration budget.
    if (r2 > bhExitRadiusSq && dot(to, rd) < 0.0) {
      break;
    }
  }

  outRayDir = rd;
  return captured;
}

// =====================================================================
// Twinkling star layer - real-time, deliberately NOT baked into the
// milky way cubemap, since baking would fix it in place and it wouldn't
// be able to twinkle anymore. This is the original starfield from before
// the black hole shader was added, unchanged other than being sampled
// with the black hole's final (possibly lensed) ray direction instead of
// the raw view ray - see main().
// =====================================================================

const float STAR_DISTANCE = 150.0;
const float STAR_DENSITY = 0.05;
const float STAR_BRIGHTNESS = 0.5;

float hash13(vec3 p3) {
  p3  = fract(p3 * vec3(.1031,.11369,.13787));
  p3 += dot(p3, p3.yzx + 19.19);
  return fract((p3.x + p3.y) * p3.z);
}

vec3 twinkleStars(in vec3 rayDir) {
  vec3 p = rayDir * STAR_DISTANCE;
  float brightness = smoothstep(1.0 - STAR_DENSITY, 1.0, hash13(floor(p)));
  float twinkle = cos(ubo.time * 2.0 + hash13(floor(p) + 17.0) * 13.0) * 0.5 + 0.5;
  return vec3(smoothstep(STAR_BRIGHTNESS, 0.0, length(fract(p) - 0.5))) * brightness * twinkle;
}

// Interleaved gradient noise (Jimenez 2014) - a cheap, texture-free
// per-pixel hash used to dither the final output. The sky here has wide,
// slowly-varying regions (background glow, the black hole's contrast
// curve) that band visibly once quantized to the 8-bit swapchain; adding
// +/- half an LSB of this noise before output breaks the bands up into
// noise instead, without needing a blue-noise texture or TAA history.
float ditherNoise(vec2 fragCoord) {
  return fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

void main()
{
  vec3 rayDir = getWorldRayDir();

  vec3 glow, disc, jets, bentRayDir;
  bool captured =
      raymarchBlackHole(rayDir, glow, disc, jets, bentRayDir);

  // Sampled with the *bent* ray direction, not the original one - this is
  // what actually makes the black hole distort/lens the sky behind it,
  // rather than just glowing on top of an undistorted background.
  vec3 background =
      texture(milkyWayCubemap, bentRayDir).rgb + twinkleStars(bentRayDir);

  // Artistic contrast, applied only to the black hole's own light so it
  // doesn't also crush the background stars.
  vec3 bhLight = glow * 0.55 + disc * 2.0 + jets * 5.0;
  bhLight = pow(max(bhLight, vec3(0.0)), vec3(1.7));

  vec3 finalColor = (captured ? vec3(0.0) : background) + bhLight;

  // Dither: +/- half an 8-bit step, decorrelated per channel so the noise
  // doesn't itself look tinted/patterned. Applied last, right before
  // output, so it dithers the actual quantization boundary rather than
  // getting washed out by any of the math above.
  vec3 dither = vec3(
      ditherNoise(gl_FragCoord.xy),
      ditherNoise(gl_FragCoord.xy + 17.0),
      ditherNoise(gl_FragCoord.xy + 37.0)
  ) - 0.5;
  finalColor += dither / 255.0;

  outColor = vec4(finalColor, 1.0);
}
