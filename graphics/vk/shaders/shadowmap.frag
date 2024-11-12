#version 460

layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;

layout(location = 0) in vec2 inUV;
layout(location = 1) flat in uint inCascadeIndex;

layout(location = 0) out vec4 outFragColor;

void main() {
    outFragColor = vec4(vec3(texture(shadowMap, vec3(inUV, inCascadeIndex)).r), 1.0f);
}