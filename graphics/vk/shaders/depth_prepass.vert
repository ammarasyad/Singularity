#version 460

#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 uv;
};

layout(set = 0, binding = 0) uniform SceneData {
    mat4 worldMatrix;
} sceneData;

layout(constant_id = 0) const uint MAX_CASCADES = 4;

layout(set = 0, binding = 1) uniform CascadeData {
    mat4 viewProjectionMatrix[MAX_CASCADES];
} cascadeData;

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform PushConstants {
    VertexBuffer vertexBuffer;
    uint cascadeIndex;
} pushConstants;

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = cascadeData.viewProjectionMatrix[pushConstants.cascadeIndex] * vec4(v.position, 1.0);
}