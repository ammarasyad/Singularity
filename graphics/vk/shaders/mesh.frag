#version 460

#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform sampler2D radianceImage;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / pc.viewportSize;
    outColor = texture(radianceImage, uv);
}