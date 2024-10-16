#version 460

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor * texture(colorTexture, fragUV).xyz, 1.0f);
}