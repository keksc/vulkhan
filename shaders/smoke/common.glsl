layout(set = 0, binding = 0, r32f) uniform image3D divergence;
layout(set = 0, binding = 1, rgba32f) uniform image3D velocities; // xyz = velX, velY, velZ (w unused)
layout(set = 0, binding = 2, r32f) uniform image3D pressures;
layout(set = 0, binding = 3, r8ui) uniform uimage3D solidCells;
layout(set = 0, binding = 4, r32f) uniform image3D dye;
layout(set = 0, binding = 5, rgba32f) uniform image3D targetVelocities;
layout(set = 0, binding = 6, r32f) uniform image3D targetDye;

// Periodic wrap - every lookup goes through this so the domain has no edges.
ivec3 wrapCoord(ivec3 p, ivec3 dims) {
  return ((p % dims) + dims) % dims;
}

float getDye(ivec3 p, ivec3 dims) {
  return imageLoad(dye, wrapCoord(p, dims)).r;
}
void setDye(ivec3 p, ivec3 dims, float d) {
  imageStore(dye, wrapCoord(p, dims), vec4(d, 0.0, 0.0, 0.0));
}

float sampleDyeAtLocalPos(vec3 pos, ivec3 dims) {
  ivec3 iPos = ivec3(floor(pos));
  vec3 t = pos - vec3(iPos);

  float c000 = getDye(iPos + ivec3(0,0,0), dims);
  float c100 = getDye(iPos + ivec3(1,0,0), dims);
  float c010 = getDye(iPos + ivec3(0,1,0), dims);
  float c110 = getDye(iPos + ivec3(1,1,0), dims);
  float c001 = getDye(iPos + ivec3(0,0,1), dims);
  float c101 = getDye(iPos + ivec3(1,0,1), dims);
  float c011 = getDye(iPos + ivec3(0,1,1), dims);
  float c111 = getDye(iPos + ivec3(1,1,1), dims);

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

// Trilinear sample of the velocity volume at a *periodic* local-space position
// (already relative to the grid origin, already wrapped to [0, dims)).
vec3 sampleVelAtLocalPos(vec3 pos, ivec3 dims) {
  ivec3 iPos = ivec3(floor(pos));
  vec3 t = pos - vec3(iPos);

  vec3 c000 = getVel(iPos + ivec3(0, 0, 0), dims);
  vec3 c100 = getVel(iPos + ivec3(1, 0, 0), dims);
  vec3 c010 = getVel(iPos + ivec3(0, 1, 0), dims);
  vec3 c110 = getVel(iPos + ivec3(1, 1, 0), dims);
  vec3 c001 = getVel(iPos + ivec3(0, 0, 1), dims);
  vec3 c101 = getVel(iPos + ivec3(1, 0, 1), dims);
  vec3 c011 = getVel(iPos + ivec3(0, 1, 1), dims);
  vec3 c111 = getVel(iPos + ivec3(1, 1, 1), dims);

  vec3 x00 = mix(c000, c100, t.x);
  vec3 x10 = mix(c010, c110, t.x);
  vec3 x01 = mix(c001, c101, t.x);
  vec3 x11 = mix(c011, c111, t.x);

  vec3 y0 = mix(x00, x10, t.y);
  vec3 y1 = mix(x01, x11, t.y);

  return mix(y0, y1, t.z);
}

// 3D Poisson has 6 neighbours (vs 4 in 2D), so the Jacobi/Gauss-Seidel
// relaxation factor is 1/6 instead of 1/4.
const float scale = 1.0 / 6.0;
const float density = 1.0;
