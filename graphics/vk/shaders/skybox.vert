#version 460

const vec3 screenQuadVertices[6] = vec3[](
    vec3(-1.0, -1.0, 0.0),
    vec3( 1.0, -1.0, 0.0),
    vec3( 1.0,  1.0, 1.0),
    vec3( 1.0,  1.0, 1.0),
    vec3(-1.0,  1.0, 0.0),
    vec3(-1.0, -1.0, 0.0)
);

layout(push_constant) uniform PushConstants {
    vec3 front;
    vec3 right;
    vec3 up;
} pushConstants;

layout(location = 0) out vec3 fragUVW;

void main() {
    vec2 pos = screenQuadVertices[gl_VertexIndex].xy;
    gl_Position = vec4(pos, 0.0, 1.0);
    fragUVW = normalize(pushConstants.front + pushConstants.right * pos.x - pushConstants.up * pos.y).xyz;
}