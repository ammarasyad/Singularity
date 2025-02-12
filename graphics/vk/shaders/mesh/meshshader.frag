#version 460
#extension GL_GOOGLE_include_directive : require

#include "../tiled_shading.glsl"

layout(set = 0, binding = 1) uniform CascadeData {
    mat4 viewProjectionMatrix[4];
} cascadeData;

layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;

layout(set = 2, binding = 0) buffer readonly lightBuffer {
    uint lightNum;
    Light lights[];
};

layout(std430, set = 2, binding = 1) buffer readonly visibilityBuffer {
    FrustumAABB frustumAABB;
    LightVisibility visibilities[];
};

layout(set = 2, binding = 2) buffer readonly lightCount {
    uint16_t lightCounts[MAX_LIGHTS_VISIBLE];
};

layout(location = 0) in VertexInput {
    vec3 inNormal;
};

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(inNormal, 1.0);
}