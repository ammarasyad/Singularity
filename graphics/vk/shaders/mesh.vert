#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_structures.glsl"

struct Vertex {
    vec3 position;
    vec3 normal;
    vec3 color;
    float uv_X;
    float uv_Y;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform PushConstants {
    mat4 worldMatrix;
    VertexBuffer vertexBuffer;
} pushConstants;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragPos;

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = sceneData.worldMatrix * pushConstants.worldMatrix * vec4(v.position, 1.0);

    fragPos = (pushConstants.worldMatrix * vec4(v.position, 1.0)).xyz;
//    fragNormal = (pushConstants.worldMatrix * vec4(v.normal, 0.0)).xyz;
    fragNormal = v.normal;
    fragColor = v.color * materialData.colorFactors.xyz;
    fragUV.x = v.uv_X;
    fragUV.y = v.uv_Y;
}