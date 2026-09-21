layout(set = 0, binding = 0, r32f) uniform image3D divergence;
layout(set = 0, binding = 1, rgba32f) uniform image3D velocities; // xyz = velX, velY, velZ (w unused)
layout(set = 0, binding = 2, r32f) uniform image3D pressures;
layout(set = 0, binding = 3, r8ui) uniform uimage3D solidCells;
layout(set = 0, binding = 4, r32f) uniform image3D dye;
layout(set = 0, binding = 5, rgba32f) uniform image3D targetVelocities;
layout(set = 0, binding = 6, r32f) uniform image3D targetDye;
// phi_hat_n scratch for MacCormack advection (see advectBackward.comp /
// macCormackCombine.comp) - holds the reversed-then-resampled estimate
// used purely as an error term, never copied back into velocities/dye.
layout(set = 0, binding = 7, rgba32f) uniform image3D scratchVelocities;
layout(set = 0, binding = 8, r32f) uniform image3D scratchDye;

// Periodic wrap - resolves ANY integer coordinate to a valid storage
// slot. This is purely an ADDRESS computation for the ring buffer (see
// the class comment on gridOrigin/index(W) = wrap(W) mod cellCount) - it
// says nothing about whether that coordinate is still inside the
// currently active window. Every lookup below still needs this to turn a
// (possibly clamped) coordinate into an actual image index, but genuine
// NEIGHBOR/SAMPLE lookups should go through clampToWindow() first - see
// its comment for why a bare wrapCoord() at those call sites was letting
// the grid's two visual edges act as if they touched.
ivec3 wrapCoord(ivec3 p, ivec3 dims) {
  return ((p % dims) + dims) % dims;
}

// Where within the currently active window [gridOrigin, gridOrigin+dims)
// a storage-space coordinate sits, per axis, continuous (not rounded),
// reduced into [0,dims). This is the window's TRUE edge location - it
// does NOT sit at storage index 0/dims (that would only be true if
// gridOrigin happened to be an exact multiple of dims); it rotates
// around the ring buffer as gridOrigin changes, at whatever residue
// gridOrigin mod dims currently is. Shared by distanceToWindowEdge (the
// existing near-edge decay) and clampToWindow (the boundary fix) below -
// both need the same quantity.
vec3 windowLocalOffset(vec3 p, ivec3 gridOrigin, ivec3 dims) {
  vec3 rel = p - vec3(gridOrigin);
  return rel - vec3(dims) * floor(rel / vec3(dims));
}

// Clamps a storage-space position `pos` - a neighbor like self +
// ivec3(1,0,0), or an arbitrarily-far backward-traced advection sample -
// to stay within the currently active local window, given the storage
// index `self` of the cell the lookup originated from.
//
// wrapCoord() alone maps ANY coordinate to a valid storage slot by pure
// modulo arithmetic - necessary to keep image accesses in bounds, but it
// can't tell a genuine one-cell-over neighbor from a lookup that's
// wandered clean off the far edge of the window: both alias, via the
// ring buffer's memory reuse, into SOME valid slot, and that slot
// generally belongs to the window's OPPOSITE face - a physically distant
// world cell, not anything actually adjacent to where the lookup
// started. THAT is what let opposite edges of the grid behave as if they
// touched.
//
// Clamping happens in CONTINUOUS window-local space - self's own
// windowLocalOffset, plus the same raw (pos - self) delta the caller
// already used to build pos - rather than via any modular reduction of
// pos itself, which would have already thrown away the "how far outside"
// information needed to tell a genuine excursion from an intentional
// address wrap. This works for pos arbitrarily far from self (a fast
// advection trace), not just one cell over (a plain 6-neighbor stencil).
//
// The result is a closed (Neumann/zero-gradient) boundary: a lookup that
// would have exited the window instead reads back whatever's at the
// window's own edge, same as if the field just stopped changing right at
// the wall - no coupling to the opposite side, and no fabricated "outside
// the world" value either.
vec3 clampToWindow(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 selfOffset = windowLocalOffset(vec3(self), gridOrigin, dims);
  vec3 delta = pos - vec3(self);
  vec3 clampedOffset =
      clamp(selfOffset + delta, vec3(0.0), vec3(dims) - vec3(1e-3));
  return vec3(self) + (clampedOffset - selfOffset);
}
ivec3 clampToWindow(ivec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  return ivec3(floor(clampToWindow(vec3(pos), self, gridOrigin, dims) + 0.5));
}

// --- targetVelocities/targetDye accessors (phi_hat) ---
// Needed by advectBackward.comp (to resample the forward pass's estimate)
// and by vorticityConfinement.comp's caller-side copy - see SmokeSys::
// update() for why vorticity writes here instead of velocities in place.
vec3 getTargetVel(ivec3 p, ivec3 dims) {
  return imageLoad(targetVelocities, wrapCoord(p, dims)).xyz;
}
float getTargetDye(ivec3 p, ivec3 dims) {
  return imageLoad(targetDye, wrapCoord(p, dims)).r;
}

float getDye(ivec3 p, ivec3 dims) {
  return imageLoad(dye, wrapCoord(p, dims)).r;
}
void setDye(ivec3 p, ivec3 dims, float d) {
  imageStore(dye, wrapCoord(p, dims), vec4(d, 0.0, 0.0, 0.0));
}

float sampleDyeAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  ivec3 iPos = ivec3(floor(pos));
  vec3 t = pos - vec3(iPos);

  float c000 = getDye(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims);
  float c100 = getDye(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims);
  float c010 = getDye(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims);
  float c110 = getDye(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims);
  float c001 = getDye(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims);
  float c101 = getDye(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims);
  float c011 = getDye(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims);
  float c111 = getDye(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims);

  float x00 = mix(c000, c100, t.x);
  float x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x);
  float x11 = mix(c011, c111, t.x);

  float y0 = mix(x00, x10, t.y);
  float y1 = mix(x01, x11, t.y);

  return mix(y0, y1, t.z);
}

bool isSolid(ivec3 p, ivec3 dims) {
  return imageLoad(solidCells, wrapCoord(p, dims)).r == 1;
}
void setVelX(ivec3 p, ivec3 dims, float velX) {
  ivec3 wp = wrapCoord(p, dims);
  vec4 cur = imageLoad(velocities, wp);
  imageStore(velocities, wp, vec4(velX, cur.y, cur.z, 0.0));
}
void setVelY(ivec3 p, ivec3 dims, float velY) {
  ivec3 wp = wrapCoord(p, dims);
  vec4 cur = imageLoad(velocities, wp);
  imageStore(velocities, wp, vec4(cur.x, velY, cur.z, 0.0));
}
void setVelZ(ivec3 p, ivec3 dims, float velZ) {
  ivec3 wp = wrapCoord(p, dims);
  vec4 cur = imageLoad(velocities, wp);
  imageStore(velocities, wp, vec4(cur.x, cur.y, velZ, 0.0));
}
void setVel(ivec3 p, ivec3 dims, vec3 vel) {
  imageStore(velocities, wrapCoord(p, dims), vec4(vel, 0.0));
}
vec3 getVel(ivec3 p, ivec3 dims) {
  return imageLoad(velocities, wrapCoord(p, dims)).xyz;
}
float getDiv(ivec3 p, ivec3 dims) {
  return imageLoad(divergence, wrapCoord(p, dims)).r;
}
void setDiv(ivec3 p, ivec3 dims, float div) {
  imageStore(divergence, wrapCoord(p, dims), vec4(div, 0.0, 0.0, 0.0));
}
float getPressure(ivec3 p, ivec3 dims) {
  return imageLoad(pressures, wrapCoord(p, dims)).r;
}
void setPressure(ivec3 p, ivec3 dims, float pressure) {
  imageStore(pressures, wrapCoord(p, dims), vec4(pressure, 0.0, 0.0, 0.0));
}

// --- Staggered (MAC) velocity component samplers ---
//
// velX(p)/velY(p)/velZ(p) each live at a DIFFERENT physical location: the
// face between cell p-1 and cell p on their own axis (i.e. exactly at the
// integer grid line, no offset), and the cell-center offset (+0.5) on the
// other two axes - this is the convention divergence.comp and
// updateVelocities.comp already assume (face differences: velX(p+1) -
// velX(p) etc.), even though all three components are packed into one
// combined image for storage.
//
// Because the three components live at three different points in space,
// there is no single "position" you can sample the combined vec3 from -
// each component needs its own trilinear sample, offset by its own
// stagger vector, pulling only its own channel out of the neighbourhood.
const vec3 STAG_X = vec3(0.0, 0.5, 0.5);
const vec3 STAG_Y = vec3(0.5, 0.0, 0.5);
const vec3 STAG_Z = vec3(0.5, 0.5, 0.0);

float sampleVelXAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 lp = pos - STAG_X;
  ivec3 iPos = ivec3(floor(lp));
  vec3 t = lp - vec3(iPos);
  float c000 = getVel(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims).x;
  float c100 = getVel(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims).x;
  float c010 = getVel(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims).x;
  float c110 = getVel(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims).x;
  float c001 = getVel(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims).x;
  float c101 = getVel(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims).x;
  float c011 = getVel(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims).x;
  float c111 = getVel(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims).x;
  float x00 = mix(c000, c100, t.x), x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x), x11 = mix(c011, c111, t.x);
  return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}
float sampleVelYAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 lp = pos - STAG_Y;
  ivec3 iPos = ivec3(floor(lp));
  vec3 t = lp - vec3(iPos);
  float c000 = getVel(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims).y;
  float c100 = getVel(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims).y;
  float c010 = getVel(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims).y;
  float c110 = getVel(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims).y;
  float c001 = getVel(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims).y;
  float c101 = getVel(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims).y;
  float c011 = getVel(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims).y;
  float c111 = getVel(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims).y;
  float x00 = mix(c000, c100, t.x), x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x), x11 = mix(c011, c111, t.x);
  return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}
float sampleVelZAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 lp = pos - STAG_Z;
  ivec3 iPos = ivec3(floor(lp));
  vec3 t = lp - vec3(iPos);
  float c000 = getVel(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims).z;
  float c100 = getVel(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims).z;
  float c010 = getVel(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims).z;
  float c110 = getVel(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims).z;
  float c001 = getVel(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims).z;
  float c101 = getVel(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims).z;
  float c011 = getVel(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims).z;
  float c111 = getVel(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims).z;
  float x00 = mix(c000, c100, t.x), x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x), x11 = mix(c011, c111, t.x);
  return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}

// Full 3-component velocity interpolated at an arbitrary continuous
// position, by sampling each component from its OWN staggered field.
// Needed wherever we want "the velocity at this point in space" (e.g. to
// know which direction to trace a face or a cell center backward/forward
// along) rather than a single component's own value.
vec3 sampleFullVelAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  return vec3(sampleVelXAtLocalPos(pos, self, gridOrigin, dims),
             sampleVelYAtLocalPos(pos, self, gridOrigin, dims),
             sampleVelZAtLocalPos(pos, self, gridOrigin, dims));
}

// --- Same, but reading targetVelocities (phi_hat) - needed by
// advectBackward.comp to resample the forward pass's estimate per
// component, at that component's own staggered location.
float sampleTargetVelXAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 lp = pos - STAG_X;
  ivec3 iPos = ivec3(floor(lp));
  vec3 t = lp - vec3(iPos);
  float c000 = getTargetVel(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims).x;
  float c100 = getTargetVel(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims).x;
  float c010 = getTargetVel(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims).x;
  float c110 = getTargetVel(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims).x;
  float c001 = getTargetVel(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims).x;
  float c101 = getTargetVel(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims).x;
  float c011 = getTargetVel(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims).x;
  float c111 = getTargetVel(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims).x;
  float x00 = mix(c000, c100, t.x), x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x), x11 = mix(c011, c111, t.x);
  return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}
float sampleTargetVelYAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 lp = pos - STAG_Y;
  ivec3 iPos = ivec3(floor(lp));
  vec3 t = lp - vec3(iPos);
  float c000 = getTargetVel(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims).y;
  float c100 = getTargetVel(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims).y;
  float c010 = getTargetVel(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims).y;
  float c110 = getTargetVel(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims).y;
  float c001 = getTargetVel(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims).y;
  float c101 = getTargetVel(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims).y;
  float c011 = getTargetVel(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims).y;
  float c111 = getTargetVel(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims).y;
  float x00 = mix(c000, c100, t.x), x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x), x11 = mix(c011, c111, t.x);
  return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}
float sampleTargetVelZAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  vec3 lp = pos - STAG_Z;
  ivec3 iPos = ivec3(floor(lp));
  vec3 t = lp - vec3(iPos);
  float c000 = getTargetVel(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims).z;
  float c100 = getTargetVel(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims).z;
  float c010 = getTargetVel(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims).z;
  float c110 = getTargetVel(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims).z;
  float c001 = getTargetVel(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims).z;
  float c101 = getTargetVel(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims).z;
  float c011 = getTargetVel(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims).z;
  float c111 = getTargetVel(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims).z;
  float x00 = mix(c000, c100, t.x), x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x), x11 = mix(c011, c111, t.x);
  return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}

vec3 sampleTargetVelAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  ivec3 iPos = ivec3(floor(pos));
  vec3 t = pos - vec3(iPos);

  vec3 c000 = getTargetVel(clampToWindow(iPos + ivec3(0, 0, 0), self, gridOrigin, dims), dims);
  vec3 c100 = getTargetVel(clampToWindow(iPos + ivec3(1, 0, 0), self, gridOrigin, dims), dims);
  vec3 c010 = getTargetVel(clampToWindow(iPos + ivec3(0, 1, 0), self, gridOrigin, dims), dims);
  vec3 c110 = getTargetVel(clampToWindow(iPos + ivec3(1, 1, 0), self, gridOrigin, dims), dims);
  vec3 c001 = getTargetVel(clampToWindow(iPos + ivec3(0, 0, 1), self, gridOrigin, dims), dims);
  vec3 c101 = getTargetVel(clampToWindow(iPos + ivec3(1, 0, 1), self, gridOrigin, dims), dims);
  vec3 c011 = getTargetVel(clampToWindow(iPos + ivec3(0, 1, 1), self, gridOrigin, dims), dims);
  vec3 c111 = getTargetVel(clampToWindow(iPos + ivec3(1, 1, 1), self, gridOrigin, dims), dims);

  vec3 x00 = mix(c000, c100, t.x);
  vec3 x10 = mix(c010, c110, t.x);
  vec3 x01 = mix(c001, c101, t.x);
  vec3 x11 = mix(c011, c111, t.x);

  vec3 y0 = mix(x00, x10, t.y);
  vec3 y1 = mix(x01, x11, t.y);

  return mix(y0, y1, t.z);
}
float sampleTargetDyeAtLocalPos(vec3 pos, ivec3 self, ivec3 gridOrigin, ivec3 dims) {
  ivec3 iPos = ivec3(floor(pos));
  vec3 t = pos - vec3(iPos);

  float c000 = getTargetDye(clampToWindow(iPos + ivec3(0,0,0), self, gridOrigin, dims), dims);
  float c100 = getTargetDye(clampToWindow(iPos + ivec3(1,0,0), self, gridOrigin, dims), dims);
  float c010 = getTargetDye(clampToWindow(iPos + ivec3(0,1,0), self, gridOrigin, dims), dims);
  float c110 = getTargetDye(clampToWindow(iPos + ivec3(1,1,0), self, gridOrigin, dims), dims);
  float c001 = getTargetDye(clampToWindow(iPos + ivec3(0,0,1), self, gridOrigin, dims), dims);
  float c101 = getTargetDye(clampToWindow(iPos + ivec3(1,0,1), self, gridOrigin, dims), dims);
  float c011 = getTargetDye(clampToWindow(iPos + ivec3(0,1,1), self, gridOrigin, dims), dims);
  float c111 = getTargetDye(clampToWindow(iPos + ivec3(1,1,1), self, gridOrigin, dims), dims);

  float x00 = mix(c000, c100, t.x);
  float x10 = mix(c010, c110, t.x);
  float x01 = mix(c001, c101, t.x);
  float x11 = mix(c011, c111, t.x);

  float y0 = mix(x00, x10, t.y);
  float y1 = mix(x01, x11, t.y);

  return mix(y0, y1, t.z);
}

// --- scratchVelocities/scratchDye accessors (phi_hat_n) ---
vec3 getScratchVel(ivec3 p, ivec3 dims) {
  return imageLoad(scratchVelocities, wrapCoord(p, dims)).xyz;
}
float getScratchDye(ivec3 p, ivec3 dims) {
  return imageLoad(scratchDye, wrapCoord(p, dims)).r;
}

// How far inside the current window `p` (a storage-index cell, i.e.
// already wrapped) sits from the nearest of the window's 6 faces, in
// cells - see SmokeSys::edgeFadeCells. Now just windowLocalOffset()
// measured against both faces - kept as its own function since every
// advection shader already calls it by this name for the decay fade.
float distanceToWindowEdge(ivec3 p, ivec3 gridOrigin, ivec3 dims) {
  vec3 offset = windowLocalOffset(vec3(p), gridOrigin, dims);
  vec3 distToNearFace = offset;
  vec3 distToFarFace = vec3(dims) - 1.0 - offset;
  vec3 distToEdge = min(distToNearFace, distToFarFace);
  return min(distToEdge.x, min(distToEdge.y, distToEdge.z));
}

// 3D Poisson has 6 neighbours (vs 4 in 2D), so the Jacobi/Gauss-Seidel
// relaxation factor is 1/6 instead of 1/4.
const float scale = 1.0 / 6.0;
const float density = 1.0;
