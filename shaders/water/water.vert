#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec3 outPos;
layout(location = 1) out vec2 outUV;

void main() {
    outPos = pos;
    outUV = uv;
}
