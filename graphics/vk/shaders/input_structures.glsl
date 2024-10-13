layout(set = 0, binding = 0) uniform SceneData{
    mat4 worldMatrix;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} sceneData;

layout(set = 1, binding = 0) uniform GLTFMaterialData{
    vec4 colorFactors;
    vec4 metalRoughFactors;
} materialData;

layout(set = 1, binding = 1) uniform sampler2D colorTexture;
layout(set = 1, binding = 2) uniform sampler2D metalRoughTexture;