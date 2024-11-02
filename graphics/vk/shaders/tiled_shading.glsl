#define TILE_X 16
#define TILE_Y 9
#define TILE_Z 24
#define MAX_LIGHTS 256

#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require

struct Light {
    f16vec4 position; // xyz: position, w: radius
    f16vec4 color; // xyz: color, w: intensity
};

struct FrustumAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

struct LightVisibility {
    uint16_t count;
    uint16_t indices[MAX_LIGHTS];
};