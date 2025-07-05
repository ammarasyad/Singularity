#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() {
    // This is a miss shader, which means it is called when no intersection is found.
    // Here we can set a default value for the ray payload.
    
    // Set the hit value to a default color (e.g., sky blue)
    hitValue = vec3(0.5, 0.7, 1.0); // Example color for the background
}