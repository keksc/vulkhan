#ifndef WORLD_DATA_GLSL
#define WORLD_DATA_GLSL
#ifndef WORLD_BINDING
#define WORLD_BINDING 1
#endif
struct WorldProfile { vec4 ground, rock, grass, foliage, controls; };
struct WorldRegion { vec4 centerRadius, selection; };
struct WorldVolcano { vec4 positionRadius, shape, plume; };
layout(set=0,binding=WORLD_BINDING,std140) uniform WorldData {
  vec4 climate, map, terrain;
  uvec4 counts;
  WorldProfile profiles[16];
  WorldRegion regions[16];
  WorldVolcano volcanoes[8];
} world;
#endif
