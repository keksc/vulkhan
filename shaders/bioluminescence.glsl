#ifndef BIOLUMINESCENCE_GLSL
#define BIOLUMINESCENCE_GLSL
// World-space habitat field: no camera, time, chunk index or resident-light list.
uint bioHash(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
  x *= 0x846ca68bu; return x ^ (x >> 16);
}
float bioRandom(ivec2 p) {
  return float(bioHash(uint(p.x)*73856093u ^ uint(p.y)*19349663u)>>8)/16777216.0;
}
// Value and analytic spatial derivative share the same four hash samples.
vec3 bioNoiseSample(vec2 p) {
  ivec2 i=ivec2(floor(p)); vec2 t=fract(p);
  vec2 f=t*t*t*(t*(t*6.0-15.0)+10.0);
  vec2 df=30.0*t*t*(t-1.0)*(t-1.0);
  float a=bioRandom(i), b=bioRandom(i+ivec2(1,0));
  float c=bioRandom(i+ivec2(0,1)), d=bioRandom(i+ivec2(1));
  return vec3(mix(mix(a,b,f.x),mix(c,d,f.x),f.y),
              mix(b-a,d-c,f.y)*df.x, mix(c-a,d-b,f.x)*df.y);
}
float bioNoise(vec2 p) { return bioNoiseSample(p).x; }
struct BioHabitat { float density; vec3 tint; vec3 direction; };
BioHabitat bioHabitat(vec2 p) {
  // Different octave orientations and domain warping break lattice patterns.
  mat2 broadRotation=mat2(.8,.6,-.6,.8);
  mat2 fineRotation=mat2(.6,-.8,.8,.6);
  vec3 broad=bioNoiseSample(broadRotation*p*.0067);
  vec2 broadGradient=transpose(broadRotation)*broad.yz*.0067;
  vec3 fine=bioNoiseSample(fineRotation*p*.027+vec2(broad.x*3.1,17.3+broad.x*1.7));
  vec2 fineGradient=transpose(fineRotation)*fine.yz*.027+
                    broadGradient*dot(fine.yz,vec2(3.1,1.7));
  vec2 gradient=broadGradient*.62+fineGradient*.38;
  BioHabitat h;
  float occupancy=smoothstep(.28,.78,broad.x*.62+fine.x*.38);
  h.density=.015+.985*occupancy*occupancy;
  h.tint=mix(vec3(1,.38,.08),vec3(1,.79,.34),smoothstep(.25,.7,fine.x));
  h.tint=mix(h.tint,vec3(.18,.75,.62),smoothstep(.72,.92,broad.x)*.65);
  // Aim toward denser emitters. The small vertical term regularizes extrema.
  h.direction=normalize(vec3(gradient.x,.003,gradient.y));
  return h;
}
vec3 bioIrradiance(BioHabitat h, vec3 N) {
  float diffuse=max((dot(N,h.direction)+.12)/1.12,0.0);
  return h.tint*(.008+.42*h.density)*(.10+.90*diffuse);
}
vec3 bioTreeIrradiance(BioHabitat h, vec3 N, float canopyAO, bool foliage) {
  // Most environmental emitters sit below the canopy. Use directional front
  // lighting and restrained leaf transmission instead of washing both sides.
  vec3 L=normalize(vec3(h.direction.x,-.65,h.direction.z));
  float ndl=dot(N,L);
  float front=max((ndl+.12)/1.12,0.0);
  float back=max((-ndl+.12)/1.12,0.0);
  float diffuse=front+(foliage ? .22*back : 0.0);
  float occlusion=mix(.10,1.0,canopyAO*canopyAO);
  return h.tint*(.008+.42*h.density)*(.035+.965*diffuse)*occlusion;
}
vec3 bioSurface(vec3 P, vec3 N, float footprint, BioHabitat h) {
  float colonies=smoothstep(.48,.73,bioNoise(P.xz*.31));
  vec2 q=P.xz*3.7; ivec2 cell=ivec2(floor(q));
  vec2 center=vec2(.15)+.7*vec2(bioRandom(cell),bioRandom(cell+ivec2(91,73)));
  float d=length(fract(q)-center);
  float aa=max(footprint*3.7,.015);
  float fleck=1.0-smoothstep(.055-aa,.055+aa,d);
  // Preserve average energy as individual flecks become subpixel.
  fleck=mix(fleck,.0095,smoothstep(.08,.4,footprint));
  float crust=colonies*(.013+fleck*.9)*(.25+.75*max(N.y,0.0));
  return h.tint*h.density*crust*3.0;
}
#endif
