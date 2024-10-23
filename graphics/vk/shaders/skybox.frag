#version 460

layout(binding = 0) uniform samplerCube skybox;

layout(location = 0) in vec3 fragUVW;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(skybox, fragUVW);
}