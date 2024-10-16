#define TILE_SIZE 16
#define MAX_LIGHTS 1024

struct Light {
    vec4 position; // xyz: position, w: radius
    vec4 color; // xyz: color, w: intensity
};

struct LightVisibility {
    uint count;
    uint indices[MAX_LIGHTS];
};

struct ViewFrustum {
    vec4 planes[6];
    vec3 points[8];
};