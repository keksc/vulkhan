#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout (location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;

vec3 palette( float t ) {
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = vec3(0.263,0.416,0.557);

    return a + b*cos( 6.28318*(c*t+d) );
}

void main() {
  const vec3 lightDir = vec3(500.0, 30.0, -1.0);
  const float ambient = 0.1;
  float lightIntensity = ambient + max(dot(normal, lightDir), 0.0);

  // vec3 lightPos = vec3(0.0, 0.0, 1.0);
  // vec3 normal = normalize(pos);
  // vec3 dirToLight = lightPos - pos;
  // vec3 cameraPosWorld = ubo.inverseView[3].xyz;
  // vec3 viewDirection = normalize(cameraPosWorld - pos);
  //
  // float attenuation = 1.0/dot(dirToLight, dirToLight);
  // // Lambertian
  // dirToLight = normalize(dirToLight);
  // float diffuseLight = max(dot(dirToLight, normal), 0.0);
  //
  // // Blinn-Phong
  // vec3 mid = normalize(viewDirection + dirToLight);
  // float specularLight = dot(dirToLight, mid);
  // specularLight = clamp(0, 1, specularLight);
  // specularLight = pow(specularLight, 512.0);
  //
  // float ambientLight = 1.0;
  //
  // outColor = texture(texSampler, uv)*((diffuseLight+specularLight)*attenuation+ambientLight);
  

  vec2 nv = uv * 2.0 - 1.0;
  vec2 nv0 = nv;
  vec3 finalColor = vec3(0.0);

  for (float i = 0.0; i < 4.0; i++) {
      nv = fract(nv * 1.5) - 0.5;

      float d = length(nv) * exp(-length(nv0));

      vec3 col = palette(length(nv0) + i*.4 + ubo.time*.4);

      d = sin(d*8. + ubo.time)/8.;
      d = abs(d);

      d = pow(0.01 / d, 1.2);

      finalColor += col * d;
  }
  outColor = texture(texSampler, uv);//*lightIntensity;
}
