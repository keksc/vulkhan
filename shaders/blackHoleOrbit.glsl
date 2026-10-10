#ifndef BLACK_HOLE_ORBIT_GLSL
#define BLACK_HOLE_ORBIT_GLSL

// Shared by sky.frag (which renders the black hole's own diorama/lensing)
// and godrays.comp (which needs to know where it lands on screen for the
// god-ray radial blur) - single source of truth for "where is the black
// hole right now". Neither shader tracks its own copy of this, so the
// two effects can never drift out of sync with each other the way two
// independently-maintained copies of this math (or worse, one of them
// mirrored into C++) previously could.
//
// Requires TWO_PI (see consts.glsl) and ubo.time (see globalUbo.glsl)
// already visible via #include before this file is included.

// How far away the black hole orbits in real world units, and how fast.
const float BH_ORBIT_RADIUS = 500.0;
const float BH_ORBIT_PERIOD_SECONDS = 120.0;

// Where the black hole actually sits in the world right now, so it can be
// orbited and parallaxed against as the camera moves.
vec3 bhWorldPos() {
  float angle = ubo.time / BH_ORBIT_PERIOD_SECONDS * TWO_PI;
  return vec3(cos(angle), 0.0, sin(angle)) * BH_ORBIT_RADIUS;
}

#endif
