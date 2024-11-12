#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#include "input_structures.glsl"
#include "tiled_shading.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const uint enablePCF = 1;
layout(constant_id = 1) const uint MAX_CASCADES = 4;

layout(set = 0, binding = 1) uniform CascadeData {
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

const float16_t ambient = 0.1hf;
const f16mat4 biasMat = f16mat4(
        0.5, 0.0, 0.0, 0.0,
        0.0, 0.5, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.5, 0.5, 0.0, 1.0
);

float16_t textureProjection(f16vec4 shadowCoord, f16vec2 offset, uint cascadeIndex) {
    const float16_t shadow = 1.0hf;
    const float16_t bias = 0.005hf;
    float16_t dist = float16_t(texture(shadowMap, vec3(shadowCoord.st + offset, cascadeIndex)).r);
//    shadow = shadowCoord.z > -1.0hf && shadowCoord.z < 1.0hf && shadowCoord.w > 0.0hf && dist < shadowCoord.z - bias ? ambient : shadow;
//    if (shadowCoord.z > -1.0hf && shadowCoord.z < 1.0hf) {
//        float16_t dist = float16_t(texture(shadowMap, f16vec3(shadowCoord.st + offset, cascadeIndex)).r);
//        if (shadowCoord.w > 0.0hf && dist < shadowCoord.z - bias) {
//            shadow = ambient;
//        }
//    }

    return shadowCoord.z > -1.0hf && shadowCoord.z < 1.0hf && shadowCoord.w > 0.0hf && dist < shadowCoord.z - bias ? ambient : shadow;
}

float16_t filterPCF(f16vec4 shadowCoord, uint cascadeIndex) {
    f16vec2 textureDim = f16vec2(textureSize(shadowMap, 0).xy);
    const float16_t scale = 0.75hf;
    f16vec2 diff = f16vec2(scale / textureDim);

    float16_t shadowFactor = 0.0hf;
    uint count = 0;
    const int range = 1;

    for (int x = -range; x <= range; x++) {
        for (int y = -range; y <= range; y++) {
            f16vec2 offset = f16vec2(x, y) * diff;
            shadowFactor += textureProjection(shadowCoord, offset, cascadeIndex);
            count++;
        }
    }

    return shadowFactor / float16_t(count);
}

void main() {
    vec4 texColor = texture(colorTexture, fragUV);
    uint cascadeIndex = 0;
    for (uint i = 0; i < MAX_CASCADES - 1; i++) {
        cascadeIndex += uint(gl_FragCoord.z < pushConstants.cascadeSplits[i]);
    }

    f16vec3 position = f16vec3(fragPos);
    f16vec4 shadowCoord = f16vec4(biasMat * cascadeData.viewProjectionMatrix[cascadeIndex] * vec4(fragPos, 1.0f));

    uint zTile = uint(TILE_Z * log((-gl_FragCoord.z - Z_NEAR) / (Z_FAR / Z_NEAR)));
    uvec3 tile = uvec3(gl_FragCoord.xy / (pushConstants.viewportSize / vec2(TILE_X, TILE_Y)), zTile);
    uint tileIndex = tile.x + tile.y * TILE_X + tile.z * TILE_X * TILE_Y;
    uint16_t numLightsInTile = lightCounts[tileIndex];

    f16vec3 normal = f16vec3(normalize(fragNormal));
    f16vec3 diffuse = f16vec3(ambient);

//    f16vec3 viewDir = f16vec3(normalize(pushConstants.cameraPosition - position));

    for (uint i = 0; i < numLightsInTile; i++) {
        Light light = lights[visibilities[tileIndex].indices[i]];

        f16vec3 lightPosition = light.position.xyz;
        f16vec4 lightColor = light.color;

        // Directional Light
        f16vec3 lightDir = normalize(lightPosition);
        float16_t lambertian = max(dot(normal, lightDir), 0.0hf);

        diffuse += lambertian * lightColor.rgb * lightColor.w;

        // Point light
//        float16_t lightDist = distance(lightPosition, position);
//        f16vec3 lightDir = (lightPosition - position) / lightDist;
//
//        float16_t lambertian = max(dot(normal, lightDir), 0.0hf);
//        float16_t distSquared = dot(lightDist, lightDist);
//        float16_t attenuation = clamp(1.0hf / distSquared, 0.0hf, 1.0hf);
//        f16vec3 halfDist = normalize(lightDir + viewDir);
//        float16_t specular = pow(clamp(dot(normal, halfDist), 0.0hf, 1.0hf), 32.0hf);
//
//        diffuse += (lambertian + specular) * lightColor.xyz * lightColor.w * attenuation;
    }

    float16_t shadow = enablePCF == 1 ? filterPCF(shadowCoord / shadowCoord.w, cascadeIndex) : textureProjection(shadowCoord / shadowCoord.w, f16vec2(0.0hf), cascadeIndex);

    outColor = vec4(diffuse * shadow, 1.0f) * texColor;

//    switch (cascadeIndex) {
//        case 0:
//            outColor *= vec4(1.0f, 0.25f, 0.25f, 1.0f);
//            break;
//        case 1:
//            outColor *= vec4(0.25f, 1.0f, 0.25f, 1.0f);
//            break;
//        case 2:
//            outColor *= vec4(0.25f, 0.25f, 1.0f, 1.0f);
//            break;
//        case 3:
//            outColor *= vec4(1.0f, 1.0f, 0.25f, 1.0f);
//            break;
//    }
}