#version 460

#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform sampler2D radianceImage;
layout(set = 0, binding = 1) uniform sampler2D accumulatedImage;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / pc.viewportSize;
    // vec4 radiance = texture(radianceImage, uv);
    // vec4 accumulated = texture(accumulatedImage, uv);

    // float alpha = 1.0 / float(pc.frameIndex + 1);
    // outColor = mix(accumulated, radiance, alpha);
    outColor = texture(radianceImage, uv);
}