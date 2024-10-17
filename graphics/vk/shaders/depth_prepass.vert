#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3 position;
    vec3 normal;
    vec3 color;
    float uv_X;
    float uv_Y;
};

layout(set = 0, binding = 0) uniform SceneData {
    mat4 worldMatrix;
} sceneData;

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform PushConstants {
    mat4 worldMatrix;
    VertexBuffer vertexBuffer;
} pushConstants;

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = sceneData.worldMatrix * pushConstants.worldMatrix * vec4(v.position, 1.0);
}