#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

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

layout(set = 2, binding = 2) buffer readonly visibilityBuffer {
    LightVisibility visibilities[];
};

layout(push_constant) uniform PushConstants {
    layout(offset = 80) mat4 viewProjection;
    layout(offset = 144) vec3 cameraPosition;
} pushConstants;

layout(early_fragment_tests) in;

void main() {
    ivec2 tile = ivec2(gl_FragCoord.xy) / ivec2(1920, 1080);
    uint tileIndex = tile.y * 120 + tile.x;
    uint numLightsInTile = visibilities[tileIndex].count;

    vec3 diffuse = texture(colorTexture, fragUV).xyz;
    vec3 normal = normalize(fragNormal);
    vec3 illumination = vec3(0.0f);

    vec3 viewDir = normalize(pushConstants.cameraPosition - fragPos);

    for (uint i = 0; i < numLightsInTile; i++) {
        uint lightIndex = visibilities[tileIndex].indices[i];

        vec3 lightPosition = lights[lightIndex].position.xyz;
        float lightRadius = lights[lightIndex].position.w;
        vec3 lightColor = lights[lightIndex].color.rgb;
        float lightIntensity = lights[lightIndex].color.w;

        vec3 lightDir = normalize(lightPosition - fragPos);

        float lambertian = max(dot(normal, lightDir), 0.0f);

        float lightDist = distance(lightPosition, fragPos);
        float spec = pow(max(dot(normal, normalize(lightDir + viewDir)), 0.0f), 32.0f);
        float attenuation = clamp(1.0f - (lightDist * lightDist) / (lightRadius * lightRadius), 0.0f, 1.0f);
        illumination += (lambertian * diffuse + spec) * lightColor * lightIntensity * attenuation;
    }

    outColor = vec4(illumination, 1.0f);
//    outColor = vec4(abs(fragNormal), 1.0f);
}