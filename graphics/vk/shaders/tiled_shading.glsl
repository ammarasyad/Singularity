#define TILE_X 16
#define TILE_Y 9
#define TILE_Z 24
#define MAX_LIGHTS 1024

#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require

struct Light {
    f16vec4 position; // xyz: position, w: radius
    f16vec4 color; // xyz: color, w: intensity
};

struct LightVisibility {
    vec4 minPoint;
    vec4 maxPoint;
    uint count;
    uint indices[MAX_LIGHTS];
};

struct ViewFrustum {
    vec4 planes[4];
//    vec4 planes[6];
//    vec3 points[8];
};