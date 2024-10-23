#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require

#include "input_structures.glsl"
#include "tiled_shading.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D depthSampler;

layout(set = 2, binding = 1) buffer readonly lightBuffer {
    uint lightNum;
    Light lights[];
};

layout(std430, set = 2, binding = 2) buffer readonly visibilityBuffer {
    LightVisibility visibilities[];
};

layout(push_constant) uniform PushConstants {
    layout(offset = 80) mat4 viewProjection;
    layout(offset = 144) vec3 cameraPosition;
    layout(offset = 160) ivec2 viewportSize;
} pushConstants;

layout(early_fragment_tests) in;

void main() {
//    uint zTile = uint((log(abs(fragPos.z) / 0.1f) * TILE_Z) / log(10000.f));
    uvec3 tile = uvec3(gl_FragCoord.xy / (pushConstants.viewportSize / vec2(TILE_X, TILE_Y)), gl_FragCoord.z);
//    uvec3 tile = uvec3(gl_FragCoord.xyz) / uvec3(pushConstants.viewportSize, 1) ;
    uint tileIndex = tile.x + tile.y * TILE_X + tile.z * TILE_X * TILE_Y;
    uint numLightsInTile = visibilities[tileIndex].count;

    f16vec3 diffuse = f16vec3(texture(colorTexture, fragUV).xyz);
    f16vec3 normal = f16vec3(normalize(fragNormal));
    f16vec3 illumination = f16vec3(0.005f) * diffuse;

    f16vec3 viewDir = f16vec3(normalize(pushConstants.cameraPosition - fragPos));

    for (uint i = 0; i < numLightsInTile; i++) {
        uint lightIndex = visibilities[tileIndex].indices[i];
        Light light = lights[lightIndex];

        f16vec3 lightPosition = light.position.xyz;
        float16_t lightRadius = light.position.w;
        f16vec3 lightColor = light.color.rgb;
        float16_t lightIntensity = light.color.w;

        f16vec3 lightDir = normalize(lightPosition - f16vec3(fragPos));

        float16_t lambertian = max(dot(normal, lightDir), float16_t(0.0f));

        float16_t lightDist = float16_t(distance(lightPosition, f16vec3(fragPos)));
        float16_t spec = pow(max(dot(normal, normalize(lightDir + viewDir)), float16_t(0.0f)), float16_t(128.0f));
        float16_t attenuation = float16_t(clamp(1.0f / (lightDist * lightDist), 0.0f, 1.0f));
        illumination += (lambertian * diffuse + spec) * lightColor * lightIntensity * attenuation;
    }

    outColor = vec4(illumination, 1.0f);
}