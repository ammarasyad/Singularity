#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct Vertex {
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 uv;
};

struct MeshAddresses {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer IndexBuffer {
    uint indices[];
};

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 2) readonly buffer MeshInfo {
    MeshAddresses meshAddresses[];
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool shadow;

hitAttributeEXT vec2 hitAttribute;

layout(push_constant) uniform PushConstants {
    layout(offset = 128) vec4 lightPosition; // xyz for position, w for intensity
    layout(offset = 144) vec4 lightColor; // RGB color, A for unused
} pc;

const float lightDistance = 10000.0; // Example light distance

#define PI 3.14159265358979323846
#define SHININESS 32.0 // Example shininess factor

vec3 computeDiffuse(vec3 lightColor, vec3 lightDir, vec3 normal) {
    return max(dot(normal, lightDir), 0.0) * lightColor;
}

vec3 computeSpecular(vec3 lightColor, vec3 viewDir, vec3 lightDir, vec3 normal) {
    const float energyConservation = (2.0 + SHININESS) / (2.0 * PI);
    vec3 halfVector = normalize(viewDir + lightDir);
    float specAngle = max(dot(normal, halfVector), 0.0);
    float specular = pow(specAngle, SHININESS) * energyConservation;
    return vec3(specular) * lightColor;
}

void main() {
    MeshAddresses mesh = meshAddresses[gl_InstanceCustomIndexEXT];

    VertexBuffer vb = VertexBuffer(mesh.vertexBufferAddress);
    IndexBuffer ib = IndexBuffer(mesh.indexBufferAddress);

    const uint primitiveIndex = gl_PrimitiveID * 3;

    uint i0 = ib.indices[primitiveIndex + 0];
    uint i1 = ib.indices[primitiveIndex + 1];
    uint i2 = ib.indices[primitiveIndex + 2];

    Vertex v0 = vb.vertices[i0];
    Vertex v1 = vb.vertices[i1];
    Vertex v2 = vb.vertices[i2];

    const vec3 barycentrics = vec3(1.0 - hitAttribute.x - hitAttribute.y,
                                    hitAttribute.x,
                                    hitAttribute.y);

    const vec3 position = barycentrics.x * v0.position + 
                          barycentrics.y * v1.position + 
                          barycentrics.z * v2.position;
    const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));

    const vec3 normal = barycentrics.x * v0.normal + 
                        barycentrics.y * v1.normal + 
                        barycentrics.z * v2.normal;
    const vec3 worldNormal = normalize(vec3(normal * gl_WorldToObjectEXT));

    // Directional light
    vec3 L = normalize(pc.lightPosition.xyz); // Light direction
    float NdotL = max(dot(worldNormal, L), 0.0);
    // vec3 L = normalize(pc.lightPosition.xyz);
    // vec3 V = normalize(gl_WorldRayOriginEXT - worldPos); // View direction

    // vec3 diffuse = computeDiffuse(pc.lightColor.rgb, L, worldNormal);

    // TODO: Implement texture sampling or other shading logic
    vec3 specular = vec3(0.0);
    float attenuation = 1.0;

    if (dot(worldNormal, L) > 0) {
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
        
        if (shadow) {
            NdotL = 0.0;
        } else {
            NdotL = NdotL;
            // specular = computeSpecular(pc.lightColor.rgb, V, L, worldNormal);
        }
    }

    // hitValue = pow(pc.lightColor.rgb * pc.lightPosition.w * NdotL + vec3(0.05), vec3(1.0 / 2.2)); // Gamma correction
    hitValue = worldNormal * 0.5 + 0.5;
}