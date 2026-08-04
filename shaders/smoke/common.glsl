layout(set = 0, binding = 0, r32f) uniform image2D divergence;
layout(set = 0, binding = 1, rg32f) uniform image2D velocities;
layout(set = 0, binding = 2, r32f) uniform image2D pressures;
layout(set = 0, binding = 3, r8ui) uniform uimage2D solidCells;

bool isSolid(int x, int y) {
  return imageLoad(solidCells, ivec2(x, y)).r == 1;
}
void setVelX(int x, int y, float velX) {
  imageStore(velocities, ivec2(x, y), vec4(velX, vec3(0.0)));
}
void setVelY(int x, int y, float velY) {
  imageStore(velocities, ivec2(x, y), vec4(0.0, velY, vec2(0.0)));
}
void setVel(int x, int y, vec2 vel) {
  imageStore(velocities, ivec2(x, y), vec4(vel, vec2(0.0)));
}
vec2 getVel(int x, int y) {
  return imageLoad(velocities, ivec2(x, y)).rg;
}
float getDiv(int x, int y) {
  return imageLoad(divergence, ivec2(x, y)).r;
}
void setDiv(int x, int y, float div) {
  imageStore(divergence, ivec2(x, y), vec4(div, vec3(0.0)));
}
float getPressure(int x, int y) {
  return imageLoad(pressures, ivec2(x, y)).r;
}
void setPressure(int x, int y, float pressure) {
  imageStore(pressures, ivec2(x, y), vec4(pressure, vec3(0.0)));
}

float sampleVelX(float x, float y, int boundX, int boundY) {
  vec2 pos = vec2(x, y);
  vec2 minBound = vec2(0.0f);
  vec2 maxBound = vec2(float(boundX) - 1.001,
                                 float(boundY) - 1.001);

  pos = clamp(pos, minBound, maxBound);

  ivec2 iPos = ivec2(floor(pos));
  vec2 t = pos - vec2(iPos);

  float mix0 = mix(getVel(iPos.x, iPos.y).r, getVel(iPos.x + 1, iPos.y).r, t.x);
  float mix1 = mix(getVel(iPos.x, iPos.y + 1).r, getVel(iPos.x + 1, iPos.y + 1).r, t.x);

  return mix(mix0, mix1, t.y);
}
float sampleVelY(float x, float y, int boundX, int boundY) {
  vec2 pos = vec2(x, y);
  vec2 minBound = vec2(0.0f);
  vec2 maxBound = vec2(float(boundX) - 1.001,
                                 float(boundY) - 1.001);

  pos = clamp(pos, minBound, maxBound);

  ivec2 iPos = ivec2(floor(pos));
  vec2 t = pos - vec2(iPos);

  float mix0 = mix(getVel(iPos.x, iPos.y).g, getVel(iPos.x + 1, iPos.y).g, t.x);
  float mix1 = mix(getVel(iPos.x, iPos.y + 1).g, getVel(iPos.x + 1, iPos.y + 1).g, t.x);

  return mix(mix0, mix1, t.y);
}

vec2 getVelAtWorldPos(vec2 worldPos, ivec2 cellCount) {
  float u = sampleVelX(worldPos.x, worldPos.y - 0.5f, cellCount.x + 1, cellCount.y);

  float v = sampleVelY(worldPos.x - 0.5f, worldPos.y, cellCount.x, cellCount.y + 1);

  return vec2(u, v);
}

const float scale = 0.25;
const float density = 1.0;
