#version 450

layout (set = 1, binding = 0) uniform samplerCube samplerCubeMap;

layout (location = 0) in vec3 UVW;

layout (location = 0) out vec4 outColor;

void main() 
{
	outColor = texture(samplerCubeMap, UVW);
}
