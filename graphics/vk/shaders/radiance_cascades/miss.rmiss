#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

const vec3 sunDirection = vec3(10.0, 6.0, 3.0);
const vec3 sunColor = vec3(1.0, 1.0, 0.95);
const float sunRadius = 0.03;
const float sunFalloff = 1.0;

const vec3 horizonColor = vec3(0.5, 0.7, 1.0);
const vec3 zenithColor = vec3(0.2, 0.4, 0.8);

void main() {
    // hitValue = vec3(0.5, 0.7, 1.0);
    vec3 rayDir = normalize(gl_WorldRayDirectionEXT);
    float dotSun = dot(rayDir, normalize(sunDirection));
    // float sunGlow = exp((dotSun - 1.0) * sunFalloff) * step(cos(sunRadius), dotSun);
    float angle = acos(clamp(dotSun, -1.0, 1.0));
    float sunGlow = exp(-pow(angle / sunRadius, 2.0) * sunFalloff);

    vec3 skyColor = mix(horizonColor, zenithColor, sqrt(max(rayDir.y, 0.0)));

    hitValue = mix(skyColor, sunColor, sunGlow);
}