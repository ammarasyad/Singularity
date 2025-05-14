#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#include "../tiled_shading.glsl"

layout(set = 0, binding = 1) uniform CascadeData {
    mat4 viewProjectionMatrix[4];
} cascadeData;

layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 8) uniform sampler2DArray textureMap;

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

layout(push_constant) uniform PushConstants {
    // layout(offset = 64) vec3 cameraPosition;
    // layout(offset = 80) ivec2 viewportSize;
    // layout(offset = 96) vec4 cascadeSplits;
    layout(offset = 80) vec3 cameraPosition;
    layout(offset = 96) ivec2 viewportSize;
    layout(offset = 112) vec4 cascadeSplits;
} pushConstants;

layout(location = 0) in VertexInput {
    vec3 inNormal;
    vec3 fragPos;
};

layout(location = 0) out vec4 outColor;

precision mediump float;

void main() {
    uint zTile = uint(TILE_Z * log((-gl_FragCoord.z - Z_NEAR) / (Z_FAR / Z_NEAR)));
    uvec3 tile = uvec3(gl_FragCoord.xy / (pushConstants.viewportSize / vec2(TILE_X, TILE_Y)), zTile);
    uint tileIndex = tile.x + tile.y * TILE_X + tile.z * TILE_X * TILE_Y;
    uint16_t numLightsInTile = lightCounts[tileIndex];

    vec3 diffuse = vec3(0.05f);
    vec3 viewDir = normalize(pushConstants.cameraPosition - inNormal);

    for (uint i = 0; i < numLightsInTile; i++) 
    {
        Light light = lights[visibilities[tileIndex].indices[i]];

        vec3 lightPosition = light.position.xyz;
        vec4 lightColor = light.color;

        float lightDist = distance(lightPosition, fragPos);
        vec3 lightDir = (lightPosition - fragPos) / lightDist;

        float lambertian = max(dot(inNormal, lightDir), 0.0f);
        float attenuation = clamp(1.0f / dot(lightDist, lightDist), 0.0f, 1.0f);
        vec3 halfDist = normalize(lightDir + viewDir);
        float specular = pow(clamp(dot(inNormal, halfDist), 0.0f, 1.0f), 32.0f);

        diffuse += (lambertian + specular) * lightColor.xyz * lightColor.w * attenuation;
    }

    outColor = vec4(inNormal, 1.0);
}