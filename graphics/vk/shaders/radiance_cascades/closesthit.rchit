#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

struct Vertex {
    vec4 position; // x, y, z, u (texcoord)
    vec4 normal; // x, y, z, v (texcoord)
};

struct MeshData {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint textureIndex;
    uint firstIndex;
};

struct LightData {
    vec4 lightPosition; // xyz for position, w for intensity
    vec4 lightColor; // RGB color, A for type (0 for directional, 1 for point)
};

layout(buffer_reference, std430) readonly buffer VertexBuffer { Vertex vertices[]; };
layout(buffer_reference, std430) readonly buffer IndexBuffer { uint indices[]; };

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 2) readonly buffer MeshInfo { MeshData meshData[]; };
layout(set = 0, binding = 3) uniform sampler2D textures[];
layout(set = 0, binding = 4) readonly buffer LightBuffer {
    uint lightCount;
    LightData lights[];
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool shadow;

hitAttributeEXT vec2 hitAttribute;

layout(push_constant) uniform PushConstants {
    layout(offset = 128) vec3 cameraPosition;
} pc;

// const float lightDistance = 10000.0;

// Fog parameters
const vec3 fogColor = vec3(0.5, 0.5, 0.5);
const float fogDensity = 0.01;
const float fogScattering = 0.2;

#define STEPS 16
#define M_PI 3.14159265358979323846 

float heyeyGreensteinPhaseFunction(float cosTheta, float g) {
    float denom = 4.0 * M_PI * pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5);
    return (1.0 - g * g) / denom;
}

vec4 applyFog(vec3 start, vec3 end, vec3 lightDir, vec3 lightColor) {
    float tMax = length(end - start);
    float dt = tMax / float(STEPS);
    vec3 rayDir = normalize(end - start);

    vec3 accumulation = vec3(0.0);
    float transmittance = 1.0;
                
    for (int i = 0; i < STEPS; i++) {
        float t = float(i) * dt;
        vec3 pos = start + rayDir * t;

        float fogFactor = exp(-fogDensity * dt);

        // shadow = true;
        // traceRayEXT(topLevelAS,
        //             gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        //             0xFF, // cull mask
        //             0, // ray flags
        //             0, // ray type
        //             1, // miss shader index
        //             pos + rayDir * 0.01,
        //             0.001, // tMin
        //             lightDir, // direction
        //             lightDistance, // tMax
        //             1); // payload location

        float visibility = shadow ? 1.0 : 0.0;
        float phase = heyeyGreensteinPhaseFunction(dot(-rayDir, normalize(lightDir)), fogScattering);

        accumulation += transmittance * phase * visibility * lightColor * fogColor * fogFactor * dt;
        // transmittance *= exp(-fogAbsorption * dt);
        transmittance *= fogFactor;
    }

    return vec4(accumulation, transmittance);
}

void main() {
    MeshData mesh = meshData[gl_InstanceCustomIndexEXT];

    VertexBuffer vb = VertexBuffer(mesh.vertexBufferAddress);
    IndexBuffer ib = IndexBuffer(mesh.indexBufferAddress);
    uint textureIndex = mesh.textureIndex;

    const uint primitiveIndex = mesh.firstIndex + gl_PrimitiveID * 3;

    const uint i0 = ib.indices[primitiveIndex + 0];
    const uint i1 = ib.indices[primitiveIndex + 1];
    const uint i2 = ib.indices[primitiveIndex + 2];

    Vertex v0 = vb.vertices[i0];
    Vertex v1 = vb.vertices[i1];
    Vertex v2 = vb.vertices[i2];

    const vec3 barycentrics = vec3(1.0 - hitAttribute.x - hitAttribute.y,
                                    hitAttribute.x,
                                    hitAttribute.y);

    const vec3 position = barycentrics.x * v0.position.xyz + 
                          barycentrics.y * v1.position.xyz + 
                          barycentrics.z * v2.position.xyz;
    const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));

    const vec3 normal = normalize(barycentrics.x * v0.normal.xyz + 
                        barycentrics.y * v1.normal.xyz + 
                        barycentrics.z * v2.normal.xyz);
    const vec3 worldNormal = normalize(vec3(normal * gl_WorldToObjectEXT));
    vec2 uv = vec2(v0.position.w, v0.normal.w) * barycentrics.x + 
                        vec2(v1.position.w, v1.normal.w) * barycentrics.y + 
                        vec2(v2.position.w, v2.normal.w) * barycentrics.z;

    vec3 texColor = (texture(textures[nonuniformEXT(textureIndex)], uv)).rgb;

    vec3 diffuse = vec3(0.0);
    for (uint i = 0; i < lightCount; i++) {
        LightData light = lights[i];
        uint lightType = uint(light.lightColor.a);

        vec3 L;
        float lightDistance;
        float lightIntensity;

        switch (lightType) {
            case 0:
                // Directional light
                L = normalize(light.lightPosition.xyz);
                lightDistance = 10000.0;
                lightIntensity = light.lightPosition.w;
                break;
            case 1:
                // Point light
                L = normalize(light.lightPosition.xyz - worldPos);
                lightDistance = length(light.lightPosition.xyz - worldPos);
                lightIntensity = light.lightPosition.w / (lightDistance * lightDistance);
                break;
        }

        float dotNL = max(dot(worldNormal, L), 0.0);

        shadow = true;
        if (dotNL > 0) {
            traceRayEXT(topLevelAS,
                        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsOpaqueEXT,
                        0xFF, // cull mask
                        0, // ray flags
                        0, // ray type
                        1, // miss shader index
                        gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT, // origin
                        0.001, // tMin
                        L, // direction
                        lightDistance, // tMax
                        1); // payload location
        }

        // vec4 fog = applyFog(pc.cameraPosition, worldPos, L, light.lightColor.rgb);
        diffuse += dotNL * lightIntensity * light.lightColor.rgb * (shadow ? vec3(0.2) : vec3(1.0));
    }

    hitValue = diffuse * texColor;
}