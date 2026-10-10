#version 450
#include "furData.glsl"
layout(location=0) out vec4 outColor;
void main() {
  ivec2 p=ivec2(gl_FragCoord.xy)-ivec2(fur.window.zw);
  if (!furInside(p)) discard;
  uint index=furPixel(p);
  uint64_t guide=conservative[index];
  if (guide==EMPTY) discard;
  float z=furDepth(guide);
  float viewDepth=furViewDepth(z);
  uint wolf=furWolf(guide);
  vec3 T=furTangent(guide);
  vec3 world=furPosition(p,z);
  vec4 c0=ubo.projView*vec4(world,1);
  vec4 c1=ubo.projView*vec4(world+T*.03,1);
  vec2 direction=(c1.xy/c1.w-c0.xy/c0.w)*ubo.resolution;
  direction/=max(length(direction),.0001);
  vec3 sum=vec3(0);
  float weight=0.0,centerHits=0.0,conservativeHits=0.0;
  vec3 base=unpackFurColor(shades[index]);
  bool hasBase=center[index]!=EMPTY;
  // Directional reconstruction: filter along fibers, reject other animals
  // and separated depth layers, estimate opacity from center/guide coverage.
  for (int y=-3;y<=3;++y) for (int x=-3;x<=3;++x) {
    ivec2 q=p+ivec2(x,y);
    if (!furInside(q)) continue;
    uint qi=furPixel(q);
    uint64_t g=conservative[qi];
    if (g==EMPTY || furWolf(g)!=wolf) continue;
    if (abs(furViewDepth(furDepth(g))-viewDepth)>max(.08,viewDepth*.002)) continue;
    vec2 d=vec2(x,y);
    float along=dot(d,direction);
    float across=length(d-direction*along);
    if (across>.75) continue;
    float w=exp(-along*along/4.5-across*across/.35);
    conservativeHits+=w;
    uint64_t hit=center[qi];
    if (hit==EMPTY || furWolf(hit)!=wolf) continue;
    if (abs(furViewDepth(furDepth(hit))-viewDepth)>max(.08,viewDepth*.002)) continue;
    vec3 sampleColor=unpackFurColor(shades[qi]);
    centerHits+=w;
    if (hasBase) w*=exp(-dot(sampleColor-base,sampleColor-base)/.81);
    sum+=sampleColor*w;
    weight+=w;
  }
  if (weight<.00001 || conservativeHits<.00001) discard;
  float alpha=clamp(centerHits/conservativeHits,0.0,1.0);
  if (alpha<.01) discard;
  outColor=vec4(sum/weight,alpha);
  gl_FragDepth=z;
}
