#version 450

layout(early_fragment_tests) in;

#include "../globalUbo.glsl"
#include "../environment.glsl"
#include "../consts.glsl"
#include "../blackHoleOrbit.glsl"

// ---------------------------------------------------------------------
// BH_MODE selects how the black hole is rendered:
//   0: closed-form lens only - analytic deflection + glow + disc/plane
//      intersection, no loop (see analyticLens()).
//   1: the original 200-step gravitational raymarch only.
//   2: HYBRID (default): analytic when the camera is far from the hole,
//      raymarch when it is close. The analytic lens models the whole bend
//      as a single deflection at the ray's closest approach, which is only
//      valid while the eye sits well outside the region where light is
//      bent (measured: ~2-4% image error at local eye distance >= 1.2,
//      but ~10x worse below ~0.8, where the eye is inside that region).
//      The switch depends only on camera distance, so it is uniform across
//      the screen: no divergence, and the marcher costs nothing at normal
//      viewing distances. Blended over BH_HYBRID_NEAR..BH_HYBRID_FAR.
// ---------------------------------------------------------------------
#define BH_MODE 2
const float BH_HYBRID_NEAR = 1.0; // local eye distance: fully raymarched below
const float BH_HYBRID_FAR  = 1.4; // fully analytic above

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
//
// bhWorldPos() (the black hole's current orbital position) now lives in
// blackHoleOrbit.glsl, shared with godrays.comp's screen-space god-ray
// pass - see that file's header comment for why.
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

// Local raymarch units per world unit, auto-derived from BH_ORBIT_RADIUS
// (see blackHoleOrbit.glsl). The SDF scene (sphere radius 0.125, disc out
// to ~1.0, step 0.02) was tuned assuming an eye a couple of local units
// away when standing near the world origin - BH_CANONICAL_EYE_DIST is
// that reference distance, so this scales world offsets down consistently
// no matter what BH_ORBIT_RADIUS is set to.
const float BH_CANONICAL_EYE_DIST = 2.0;
const float BH_WORLD_TO_LOCAL = BH_CANONICAL_EYE_DIST / BH_ORBIT_RADIUS;

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

// Interleaved gradient noise (Jimenez 2014) - a cheap, texture-free
// per-pixel hash. Used both to jitter the raymarch below (breaking up
// step-count banding into noise) and, further down, to dither the final
// output color (breaking up 8-bit quantization banding into noise) - same
// underlying trick applied at two different stages of the shader.
float ditherNoise(vec2 fragCoord) {
  return fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

#if BH_MODE != 0
// Adaptive step size for the raymarch below, sphere-traced against the
// scene's two surfaces (event-horizon sphere + accretion disc torus)
// instead of the old fixed 0.02-per-iteration step. `s` is the sphere
// SDF at the current (pre-step) position, `torusDist` an (inexact, due
// to the non-uniform y-scale used elsewhere for the oblate disc, but
// good enough as a step-size heuristic) distance estimate to the disc.
// Stepping by ~half the distance to the nearer surface lets rays that
// pass nowhere near the black hole cross the whole scene in a handful
// of iterations, while keeping the original fine step size right where
// visual detail actually lives: close to the sphere and disc. Clamped
// to [0.004, 0.08] so it never fully stops near a surface (min) or
// tunnels through the thin disc on a single step from far away (max).
float bhStepSize(in vec3 p) {
  float s = abs(sdfSphere(p, bhSphere));
  float torusDist = abs(sdfTorus(p * vec3(1.0, 25.0, 1.0), bhTorus));
  return clamp(min(s, torusDist) * 0.5, 0.004, 0.08);
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
  // current world position (bhWorldPos(), see blackHoleOrbit.glsl) - this
  // is what gives the effect real parallax as the player approaches.
  // World-space camera position is the translation column of inverseView
  // (view -> world); the offset is compressed into the raymarch's local
  // unit space via BH_WORLD_TO_LOCAL, then its *distance* (not direction)
  // is smoothly clamped to BH_MAX_EYE_DIST via smin() - so up close the
  // eye moves (almost) freely for real parallax, and past that distance
  // it gradually stops receding and just holds its direction, keeping the
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

  // Adaptive step size makes how-many-iterations-until-a-given-shell
  // strongly correlated between neighboring pixels (since step size
  // depends smoothly on radial distance) - visible as concentric rings
  // where iteration count ticks over by +/-1 across a pixel boundary.
  // Jittering the very first step by a random fraction of a step
  // (per-pixel, via the same cheap hash used for output dithering below)
  // decorrelates that between neighbors, turning the rings into noise
  // instead - much less visually objectionable, and it only touches the
  // starting offset, not the per-step physics, so it doesn't reopen the
  // integration-accuracy issue dtScale fixed.
  p += rd * bhStepSize(p) * ditherNoise(gl_FragCoord.xy);

  // Squared version of the early-out radius, reused inside the loop below
  // to detect "ray has passed the black hole and is now receding past the
  // point where anything else it does matters" - avoids burning the rest
  // of the iteration budget on rays that grazed past instead of heading
  // toward the horizon or disc.
  float bhExitRadiusSq = (BH_MAX_EYE_DIST + 0.5) * (BH_MAX_EYE_DIST + 0.5);

  // Same 200-iteration ceiling as before (was 1.0 / 0.005), now a plain
  // iteration cap rather than a fixed-distance budget, since step size
  // varies. Adaptive stepping means rays that pass nowhere near the
  // sphere/disc converge on the exit condition in far fewer iterations
  // than this in practice; the cap just remains as a worst-case bound.
  const int BH_MAX_STEPS = 200;

  // The gravity-bending increment and every glow/disc/jets accumulation
  // below are per-ITERATION increments, not per-unit-distance ones - they
  // were tuned assuming every step is exactly this long (the original
  // fixed 0.02). With adaptive stepping that assumption breaks: a big
  // step through empty space would get the same bending kick and light
  // accumulation as a tiny step right next to the horizon, which shows up
  // as kinked/incorrect lensing and visible banding wherever step size
  // transitions. Scaling every accumulation by dtScale (how long *this*
  // step actually was, relative to the original tuning) turns each sum
  // back into a proper Riemann-sum approximation of the same integral,
  // independent of step size.
  const float BH_REFERENCE_STEP = 0.02;

  for (int i = 0; i < BH_MAX_STEPS; ++i) {
    float stepSize = bhStepSize(p);
    float dtScale = stepSize / BH_REFERENCE_STEP;
    p += rd * stepSize * noncaptured;

    vec3 to = - p;
    float r2 = dot(to, to);
    rd += normalize(to) * (bhSphere.mass / r2) * dtScale;

    float s = sdfSphere(p, bhSphere);
    noncaptured = smoothstep(0.0, 0.666, s);

    glow += vec3(1.0, 0.9, 0.85) * (1.0 / r2) * 0.0030 * dtScale * noncaptured;

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
          discColor * smoothstep(0.0, 1.0, -torusDist) * noncaptured * dtScale
      );

      jets += vec3(0.5, 0.7, 0.99) * (1.0 / discRadius) * noncaptured * 0.0001 * dtScale;
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

#endif // BH_MODE != 0

// =====================================================================
// Analytic ("fisheye-style") black hole - replaces the raymarch above.
//
// Idea: instead of integrating the ray step by step, treat the whole
// deflection as ONE bend at the ray's closest approach to the hole, using
// the closed-form integral of the same 1/r^2 pull the marcher applies
// (K = mass / BH_REFERENCE_STEP = 0.125 per unit length), integrated only
// over the stretch the marcher actually walks (eye -> r = BH_R_OUT, the
// marcher's own exit radius). Everything else is then a couple of ray/
// plane intersections against the same disc the SDF describes.
//
// Constants below were fitted numerically against a numpy port of the
// raymarch above (camera distance 1.0-2.0, all elevations):
//   deflection  ~1 deg RMS for impact parameter b > 0.7
//   glow        ~7% RMS
//   capture     b < 0.47 (measured edge 0.46-0.48)
//   near-hole   the marcher's slowdown ("noncaptured") makes deflection
//               blow up close to the hole; modelled as an exponential.
// Not modelled: the marcher's step-size banding rings (a marcher artifact),
// and light bent more than once around the hole (higher-order images).
// =====================================================================

const float BH_R_OUT      = 2.5;           // marcher's exit radius
const float BH_PULL       = 0.125 * 1.1256; // mass/0.02, times fitted scale
const float BH_CAPTURE_B  = 0.47;          // impact parameter of the shadow
const float BH_BOOST_A    = 1.10;          // near-hole extra deflection (rad)
const float BH_BOOST_B0   = 0.4877;
const float BH_BOOST_W    = 0.0544;
const float BH_GLOW_G     = 0.1683;
// Glow over the shadow itself, fitted to the marcher's captured rays:
// ~0.42 at the centre, rising smoothly toward the rim (a flat constant here
// read as a uniform grey disc with a hard edge, especially seen from above).
const float BH_GLOW_IN0   = 0.42;
const float BH_GLOW_IN_A  = 0.65;
const float BH_GLOW_IN_W  = 0.066;

// Local eye used by the lens - identical to the derivation in
// raymarchBlackHole() (real camera offset, distance smoothly clamped).
vec3 bhLocalEye() {
  vec3 cameraWorldPos = ubo.inverseView[3].xyz;
  vec3 offset = (cameraWorldPos - bhWorldPos()) * BH_WORLD_TO_LOCAL;
  float offsetDist = length(offset);
  float clampedDist = smin(offsetDist, BH_MAX_EYE_DIST, BH_EYE_SOFTEN);
  return offset * (clampedDist / max(offsetDist, 0.0001));
}

// Weight of ONE crossing of the disc's mid-plane (y = 0) by the segment
// pos + t*d, t in (0, tmax). Equivalent to the marcher's
//   sum(smoothstep(0,1,-torusDist) * noncaptured * dtScale)
// over the slab the segment passes through: the slab profile integrates
// to 0.9725 * (1 - (a/0.99)^2)^2.763 (a = ring-radius offset, fitted,
// max error 0.008), divided by 25 (the SDF's y stretch) and by
// BH_REFERENCE_STEP = 0.02 -> factor 2.0, over |d.y| for the slant.
float discCrossing(vec3 pos, vec3 d, float tmax, out vec3 h) {
  float dy = d.y;
  float t = -pos.y / (abs(dy) < 1e-6 ? 1e-6 : dy);
  h = pos + t * d;
  float rho = length(h.xz);
  float a = rho - bhTorus.minor;
  float q = clamp(1.0 - (a / 0.99) * (a / 0.99), 0.0, 1.0);
  float slab = 0.9725 * pow(q, 2.763);
  float nc = smoothstep(0.0, 0.666, length(h) - bhSphere.radius);
  float valid = step(0.0, t) * step(t, tmax) * step(rho, 1.79);
  // 0.2 floor on |d.y|: grazing rays would otherwise blow up; the marcher's
  // path through the slab is bounded by the disc's radial extent anyway.
  return valid * 2.0 * slab / max(abs(dy), 0.2) * nc;
}

// Shades the disc at world-local hit point h (same formulas as the loop in
// raymarchBlackHole). textureLod(0): the disc noise is heavily magnified
// on screen (the hole always covers ~100+ px), and hits are evaluated
// under divergent control flow, so implicit derivatives would be junk.
vec3 discShade(vec3 h) {
  vec3 to = -h;
  float s = length(h) - bhSphere.radius;
  float discRadius = length(to.xz);
  float discAngle = atan(to.x, to.z);

  float discFreqRaw = 0.01 + (discRadius - bhSphere.radius) * 0.002;
  float discCycles = round(discFreqRaw * 20.0 * PI);
  float discFreq = discCycles / (20.0 * PI);

  vec2 discSample = vec2(
      discRadius,
      discAngle * discFreq + 2.0 * PI + -ubo.time * 0.005
  ) * vec2(10.0, 20.0);

  float discNoise = textureLod(discTurbulence, discSample * vec2(0.1, 0.5), 0.0).r;
  float discMask = max(0.0, discNoise + 0.05) * (4.0 / (0.001 + s * 50.0));
  return mix(vec3(1.0, 0.8, 0.6), vec3(0.5, 0.46, 0.4), s * s) * discMask;
}

// Outputs match raymarchBlackHole() except `visible` (1 = background
// shows through, 0 = inside the shadow; soft-edged) replaces the
// `captured` bool.
void analyticLens(in vec3 rayDir, out vec3 glow, out vec3 disc,
                  out vec3 jets, out vec3 outRayDir, out float visible) {
  vec3 eye = bhLocalEye();

  float t0 = dot(-eye, rayDir);          // param of closest approach
  // A ray with t0 <= 0 is already past its closest approach: it is moving
  // AWAY from the hole, and the marcher only ever sees it drift out from
  // r = |eye| to the exit radius. The shadow, the near-hole deflection
  // boost and the halo belong to rays heading TOWARD the hole only -
  // without this gate the line's closest approach also "captures" rays
  // pointing the opposite way, drawing a ghost second hole behind you.
  bool inbound = t0 > 0.0;

  vec3 cp = eye + t0 * rayDir;           // closest point on the line
  float b = max(length(cp), 1e-3);       // impact parameter
  float s0 = -t0;                        // start, relative to cp
  float B2 = b * b;
  float s1 = sqrt(max(BH_R_OUT * BH_R_OUT - B2, 1e-6)); // exit
  float r0 = sqrt(B2 + s0 * s0);
  float r1 = sqrt(B2 + s1 * s1);

  // Closed form of the integral of (p / r^3) along the walked stretch:
  // perpendicular part (toward the hole) and parallel part (along the ray).
  float perp = max(BH_PULL * (1.0 / B2) * (s1 / r1 - s0 / r0) * b, 0.0);
  float par  = BH_PULL * (1.0 / r0 - 1.0 / r1);
  float bc = max(b, BH_CAPTURE_B);
  float boost = inbound ? BH_BOOST_A * exp(-(bc - BH_BOOST_B0) / BH_BOOST_W) : 0.0;
  float ang = atan(perp, 1.0 - par) + boost;

  vec3 chat = cp / b;
  vec3 bent = normalize(cos(ang) * rayDir - sin(ang) * chat);
  outRayDir = bent;

  visible = inbound
      ? smoothstep(BH_CAPTURE_B - 0.015, BH_CAPTURE_B + 0.015, b)
      : 1.0;

  float glowOutside = BH_GLOW_G * (atan(s1 / b) - atan(s0 / b)) / b +
                      (inbound ? 0.87 * exp(-(bc - 0.47) / 0.06) : 0.0);
  float glowInside = BH_GLOW_IN0 + BH_GLOW_IN_A *
      exp((min(b, BH_CAPTURE_B) - BH_CAPTURE_B) / BH_GLOW_IN_W);
  glow = vec3(1.0, 0.9, 0.85) * mix(glowInside, glowOutside, visible);

  // Disc: up to two crossings of the mid-plane.
  //  1) eye -> closest approach, straight. NOT masked by the shadow: the
  //     near side of the disc lies in FRONT of the hole and is seen over it
  //     (it is the bright band across the shadow) - the marcher accumulates
  //     it before the ray is ever captured.
  //  2) closest approach -> exit along the bent ray. Only for rays that
  //     escape (visible); this is the lensed halo of the disc's far side.
  disc = vec3(0.0);
  jets = vec3(0.0);
  vec3 h;
  if (inbound) {
    float w1 = discCrossing(eye, rayDir, t0, h);
    if (w1 > 0.0) {
      disc += discShade(h) * w1;
      jets += vec3(0.5, 0.7, 0.99) * (1.0 / max(length(h.xz), 1e-3)) * 0.0001 * w1;
    }
  }
  vec3 pos2 = inbound ? cp : eye;
  float len2 = inbound ? s1 : (s1 - s0);
  float w2 = discCrossing(pos2, bent, len2, h) * visible;
  if (w2 > 0.0) {
    disc += discShade(h) * w2;
    jets += vec3(0.5, 0.7, 0.99) * (1.0 / max(length(h.xz), 1e-3)) * 0.0001 * w2;
  }
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

// Turns one evaluation of the black hole (either implementation) into the
// final pixel color: lensed background (or black inside the shadow) plus the
// black hole's own light.
vec3 composeSky(vec3 glow, vec3 disc, vec3 jets, vec3 bentRayDir,
                float bgVisible) {
  // Sampled with the *bent* ray direction, not the original one - this is
  // what actually makes the black hole distort/lens the sky behind it,
  // rather than just glowing on top of an undistorted background.
  vec3 background =
      texture(milkyWayCubemap, bentRayDir).rgb + twinkleStars(bentRayDir);

  // Artistic contrast, applied only to the black hole's own light so it
  // doesn't also crush the background stars.
  vec3 bhLight = glow * 0.55 + disc * 2.0 + jets * 5.0;
  bhLight = pow(max(bhLight, vec3(0.0)), vec3(1.7));

  return background * bgVisible + bhLight;
}

// A magnetic furnace: spherical plasma under a broken, petal-like corona.
// All angular harmonics are integral so the atan branch cut stays seamless.
vec3 detailedMoon(vec3 rayDir) {
  vec3 axis = sunDirection();
  vec3 reference = abs(axis.y)<.95 ? vec3(0,1,0) : vec3(1,0,0);
  vec3 right = normalize(cross(reference,axis));
  vec3 up = cross(axis,right);
  vec2 p = vec2(dot(rayDir,right),dot(rayDir,up)) / sin(.018);
  float r = length(p);
  float aa = max(length(fwidth(p)),.001);
  if (dot(rayDir,axis)<=0 || r>1.0+aa) return vec3(0);
  float disk = 1.0-smoothstep(1.0-aa,1.0+aa,r);
  vec3 normal = vec3(p,sqrt(max(1.0-dot(p,p),0.0)));
  float phase = max(dot(normal,normalize(vec3(-.6,.25,.8))),0.0);
  float maria = weatherNoise(p*5.0)*.6 + weatherNoise(p*17.0)*.4;
  float crater = weatherNoise(p*45.0);
  vec3 lunar = vec3(.48,.55,.68) * (.35+.65*maria) * (.9+.1*crater);
  return lunar * (.08+.92*phase) * disk;
}

void main()
{
  vec3 rayDir = getWorldRayDir();

  vec3 glow, disc, jets, bentRayDir;
  float bgVisible;
  vec3 finalColor;

#if BH_MODE == 1
  bool captured = raymarchBlackHole(rayDir, glow, disc, jets, bentRayDir);
  finalColor = composeSky(glow, disc, jets, bentRayDir, captured ? 0.0 : 1.0);
#elif BH_MODE == 0
  analyticLens(rayDir, glow, disc, jets, bentRayDir, bgVisible);
  finalColor = composeSky(glow, disc, jets, bentRayDir, bgVisible);
#else
  // wFar is the same for every pixel of the frame (it only depends on the
  // camera), so these branches are uniform; both run only inside the short
  // blend band.
  float wFar = smoothstep(BH_HYBRID_NEAR, BH_HYBRID_FAR, length(bhLocalEye()));
  vec3 nearColor = vec3(0.0);
  vec3 farColor = vec3(0.0);
  if (wFar < 1.0) {
    bool captured = raymarchBlackHole(rayDir, glow, disc, jets, bentRayDir);
    nearColor = composeSky(glow, disc, jets, bentRayDir, captured ? 0.0 : 1.0);
  }
  if (wFar > 0.0) {
    analyticLens(rayDir, glow, disc, jets, bentRayDir, bgVisible);
    farColor = composeSky(glow, disc, jets, bentRayDir, bgVisible);
  }
  finalColor = mix(nearColor, farColor, wFar);
#endif

  finalColor *= 0.22; // Preserve celestial detail at nighttime brightness.

  // Keep the existing celestial sky, with a shared atmospheric horizon and
  // a sun halo that agrees with surface lighting and fog scattering.
  float horizon = exp(-max(rayDir.y, 0.0) * 5.0);
  float heightDensity = exp(-max(ubo.inverseView[3].y - ubo.fogParams.z, 0.0) *
                            ubo.fogParams.y);
  float haze = (1.0 - exp(-ubo.fogParams.x * 900.0)) * horizon * heightDensity;
  finalColor = mix(finalColor, fogInscatter(rayDir), clamp(haze, 0.0, 0.85));
  finalColor += detailedMoon(rayDir) * (1.0 - haze * 0.4);

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
