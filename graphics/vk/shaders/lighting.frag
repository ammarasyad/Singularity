#version 460

#extension GL_GOOGLE_include_directive : require
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
    uint zTile = uint((log(abs(gl_FragCoord.z) / 0.1f) * TILE_Z) / log(10000.f));
    uvec3 tile = uvec3(gl_FragCoord.xy / (pushConstants.viewportSize / vec2(TILE_X, TILE_Y)), zTile);
//    uvec3 tile = uvec3(gl_FragCoord.xyz) / uvec3(pushConstants.viewportSize, 1) ;
    uint tileIndex = tile.x + tile.y * TILE_X + tile.z * TILE_X * TILE_Y;
    uint numLightsInTile = visibilities[tileIndex].count;

    f16vec3 diffuse = f16vec3(texture(colorTexture, fragUV).xyz);
    f16vec3 normal = f16vec3(normalize(fragNormal));
    f16vec3 illumination = f16vec3(0.005hf) * diffuse;

    f16vec3 viewDir = f16vec3(normalize(pushConstants.cameraPosition - fragPos));

    for (uint i = 0; i < numLightsInTile; i++) {
        Light light = lights[visibilities[tileIndex].indices[i]];

        f16vec3 lightPosition = light.position.xyz;
        f16vec4 lightColor = light.color;

        float16_t lightDist = distance(lightPosition, f16vec3(fragPos));
        f16vec3 lightDir = (lightPosition - f16vec3(fragPos)) / lightDist;

        f16vec2 lambertianAttenuation = f16vec2(max(dot(normal, lightDir), 0.0hf), 1.0hf / dot(lightDist, lightDist));
        float16_t spec = pow(max(dot(normal, normalize(lightDir + viewDir)), 0.0hf), 128.0hf);
        illumination += (lambertianAttenuation.x * diffuse + spec) * lightColor.xyz * lightColor.w * lambertianAttenuation.y;
    }

    outColor = vec4(illumination, 1.0f);
}