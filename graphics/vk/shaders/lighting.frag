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

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) buffer readonly lightBuffer {
    uint lightNum;
    Light lights[];
};

layout(std430, set = 2, binding = 1) buffer readonly visibilityBuffer {
    FrustumAABB frustumAABB;
    LightVisibility visibilities[];
};

layout(push_constant) uniform PushConstants {
    layout(offset = 80) vec3 cameraPosition;
    layout(offset = 96) ivec2 viewportSize;
} pushConstants;

layout(early_fragment_tests) in;

void main() {
    uint zTile = uint(TILE_Z * log((-gl_FragCoord.z - 0.1f) / 10000.f));
    uvec3 tile = uvec3(gl_FragCoord.xy / (pushConstants.viewportSize / vec2(TILE_X, TILE_Y)), zTile);
//    uvec3 tile = uvec3(gl_FragCoord.xyz) / uvec3(pushConstants.viewportSize, 1) ;
    uint tileIndex = tile.x + tile.y * TILE_X + tile.z * TILE_X * TILE_Y;
    uint16_t numLightsInTile = visibilities[tileIndex].count;

    f16vec3 position = f16vec3(fragPos);
    f16vec3 normal = f16vec3(normalize(fragNormal));
    f16vec3 diffuse = f16vec3(0.005hf);

    f16vec3 viewDir = f16vec3(normalize(pushConstants.cameraPosition - position));

    for (uint i = 0; i < numLightsInTile; i++) {
        Light light = lights[visibilities[tileIndex].indices[i]];

        f16vec3 lightPosition = light.position.xyz;
        f16vec4 lightColor = light.color;

        float16_t lightDist = distance(lightPosition, position);
        f16vec3 lightDir = (lightPosition - position) / lightDist;

        float16_t lambertian = max(dot(normal, lightDir), 0.0hf);
        float16_t distSquared = dot(lightDist, lightDist);
        float16_t attenuation = clamp(1.0hf / distSquared, 0.0hf, 1.0hf);
        f16vec3 halfDist = normalize(lightDir + viewDir);
        float16_t specular = pow(clamp(dot(normal, halfDist), 0.0hf, 1.0hf), 32.0hf);

        diffuse += (lambertian + specular) * lightColor.xyz * lightColor.w * attenuation;
    }

    outColor = vec4(diffuse , 1.0f) * texture(colorTexture, fragUV);
}