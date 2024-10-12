#version 460

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    float lightValue = max(dot(fragNormal, sceneData.sunlightDirection.xyz), 0.1f);

    vec3 color = fragColor * texture(colorTexture, fragUV).xyz;
    vec3 ambient = color * sceneData.ambientColor.xyz;

    outColor = vec4(color * lightValue * sceneData.sunlightColor.w + ambient, 1.0f);
}