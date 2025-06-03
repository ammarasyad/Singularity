#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#include "input_structures.glsl"
#include "tiled_shading.glsl"

precision mediump float;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const uint enablePCF = 1;
layout(constant_id = 1) const uint MAX_CASCADES = 4;

layout(set = 0, binding = 1) uniform readonly CascadeData {
    mat4 viewProjectionMatrix[MAX_CASCADES];
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

layout(push_constant) uniform PushConstants {
    layout(offset = 80) vec3 cameraPosition;
    layout(offset = 96) ivec2 viewportSize;
    layout(offset = 112) vec4 cascadeSplits;
} pushConstants;

layout(early_fragment_tests) in;

const float ambient = 0.1f;
const mat4 biasMat = mat4(
        0.5, 0.0, 0.0, 0.0,
        0.0, 0.5, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.5, 0.5, 0.0, 1.0
);

float textureProjection(vec4 shadowCoord, vec2 offset, uint cascadeIndex) {
    const float shadow = 1.0f;
    const float bias = 0.005f;
    float dist = texture(shadowMap, vec3(shadowCoord.st + offset, cascadeIndex)).r;

    return shadowCoord.z > -1.0f && shadowCoord.z < 1.0f && shadowCoord.w > 0.0f && dist < shadowCoord.z - bias ? ambient : shadow;
}

float filterPCF(vec4 shadowCoord, uint cascadeIndex) {
    vec2 textureDim = textureSize(shadowMap, 0).xy;
    const float scale = 0.75f;
    vec2 diff = vec2(scale / textureDim.x, scale / textureDim.y);

    float shadowFactor = 0.0f;
    uint count = 0;
    const int range = 1;

    for (int x = -range; x <= range; x++) {
        for (int y = -range; y <= range; y++) {
            vec2 offset = vec2(x, y) * diff;
            shadowFactor += textureProjection(shadowCoord, offset, cascadeIndex);
            count++;
        }
    }

    return shadowFactor / count;
}

void main() {
    vec4 texColor = texture(colorTexture, fragUV);
    uint cascadeIndex = 0;
    for (uint i = 0; i < MAX_CASCADES - 1; i++) {
        cascadeIndex += uint(fragPos.z < pushConstants.cascadeSplits[i]);
    }

    vec4 shadowCoord = biasMat * cascadeData.viewProjectionMatrix[cascadeIndex] * vec4(fragPos, 1.0f);

    uint zTile = uint(TILE_Z * log((-fragPos.z - Z_NEAR) / (Z_FAR / Z_NEAR)));
    uvec3 tile = uvec3(fragPos.xy / (pushConstants.viewportSize / vec2(TILE_X, TILE_Y)), zTile);
    uint tileIndex = tile.x + tile.y * TILE_X + tile.z * TILE_X * TILE_Y;
    uint16_t numLightsInTile = lightCounts[tileIndex];

    vec3 normal = normalize(fragNormal);
    vec3 diffuse = vec3(ambient);

    float shadow = enablePCF == 1 ? filterPCF(shadowCoord / shadowCoord.w, cascadeIndex) : textureProjection(shadowCoord / shadowCoord.w, vec2(0.0f), cascadeIndex);
    float shadowDiffuse = max(dot(normal, normalize(-vec3(1.0f, -4.0f, 1.0f))), ambient) * shadow;
    for (uint i = 0; i < numLightsInTile; i++) {
        Light light = lights[visibilities[tileIndex].indices[i]];

        vec3 lightPosition = light.position.xyz;
        vec4 lightColor = light.color;

        float lightDist = distance(lightPosition, fragPos);
        vec3 lightDir = (lightPosition - fragPos) / lightDist;
        vec3 halfDist = normalize(lightDir + normalize(lightPosition));

        float lambertian = max(dot(normal, lightDir), 0.0f);
        float attenuation = clamp(1.0f / dot(lightDist, lightDist), 0.0f, 1.0f);
        float specular = pow(clamp(dot(normal, halfDist), 0.0f, 1.0f), 32.0f);

        diffuse += (lambertian * (1 - shadow) + specular) * lightColor.rgb * lightColor.w * attenuation;
    }

    // outColor = vec4(diffuse * (shadow), 1.0f) * texColor;
    outColor = vec4(diffuse, 1.0f) * texColor;
    // outColor = vec4(vec3(textureProjection(shadowCoord / shadowCoord.w, vec2(0.0f), 2)), 1.0f);
    // switch (cascadeIndex) {
    //     case 0:
    //         outColor *= vec4(1.0f, 0.25f, 0.25f, 1.0f);
    //         break;
    //     case 1:
    //         outColor *= vec4(0.25f, 1.0f, 0.25f, 1.0f);
    //         break;
    //     case 2:
    //         outColor *= vec4(0.25f, 0.25f, 1.0f, 1.0f);
    //         break;
    //     case 3:
    //         outColor *= vec4(1.0f, 0.25f, 1.0f, 1.0f);
    //         break;
    // }
}