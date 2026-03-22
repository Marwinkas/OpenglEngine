#version 450
#extension GL_EXT_nonuniform_qualifier : require 

layout (location = 0) out vec4 gNormalRoughness;
layout (location = 1) out vec4 gAlbedoMetallic;
layout (location = 2) out vec4 gHeightAO; 

layout (location = 0) in vec3 inCrntPos;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in mat3 inTBN;
layout (location = 5) flat in uint inMatID;

// ЭТУ ПЕРЕМЕННУЮ ТЫ УЖЕ ПЕРЕДАЕШЬ ИЗ C++!
layout(set = 0, binding = 0) uniform GlobalSceneUbo {
    mat4 projection;
    mat4 invViewProj;
    mat4 view;
    vec3 camPos;
    float zNear;
    vec3 sunDir;
    float zFar;
    vec4 screenSize; // x, y = width, height
    mat4 sunLightSpaceMatrices[4];
    float cascadeSplits[4];
    uint gridDimX;
    uint gridDimY;
    uint gridDimZ;
    float lightSize; // <-- ДОБАВЛЯЕМ СЮДА
} ubo;
struct MaterialData {
    int albedoIdx;
    int normalIdx;
    int heightIdx;
    int metallicIdx;
    int roughnessIdx;
    int aoIdx;
    int hasAlbedo, hasNormal, hasHeight, hasMetallic, hasRoughness, hasAO;
    int useTriplanar;
    float triplanarScale;
    vec2 uvScale;
};

layout(std430, set = 1, binding = 1) readonly buffer MaterialBlock {
    MaterialData materials[];
} matBuffer;

// SET 2: Массив всех текстур (Bindless Vulkan)
layout(set = 2, binding = 0) uniform sampler2D allTextures[];

void main() {
    MaterialData mat = matBuffer.materials[inMatID];
    vec2 finalUV = inTexCoord * mat.uvScale;
    
    vec3 N = normalize(inTBN[2]);
    mat3 TBN = inTBN;

    // Параллакс (Height)
    float height = 0.0;
    if (mat.hasHeight == 1) {
        vec3 camPos = ubo.invViewProj[3].xyz;
        vec3 viewDirWorld = normalize(camPos - inCrntPos);
        vec3 viewDirTangent = normalize(transpose(TBN) * viewDirWorld);
        
        // Читаем через индекс из массива
        height = texture(allTextures[nonuniformEXT(mat.heightIdx)], finalUV).r;
        finalUV -= viewDirTangent.xy * (height * 0.02);
    }

    // Albedo
    vec3 albedo = vec3(0.8);
    if (mat.hasAlbedo == 1) {
        albedo = texture(allTextures[nonuniformEXT(mat.albedoIdx)], finalUV).rgb;
    }

    // Normal Mapping
    vec3 worldNormal = N;
    if (mat.hasNormal == 1) {
        vec3 tangentNormal = texture(allTextures[nonuniformEXT(mat.normalIdx)], finalUV).xyz * 2.0 - 1.0;
        worldNormal = normalize(TBN * tangentNormal);
    }

    // Остальные карты
    float roughness = (mat.hasRoughness == 1) ? texture(allTextures[nonuniformEXT(mat.roughnessIdx)], finalUV).r : 0.9;
    float metallic = (mat.hasMetallic == 1) ? texture(allTextures[nonuniformEXT(mat.metallicIdx)], finalUV).r : 0.0;
    float ao = (mat.hasAO == 1) ? texture(allTextures[nonuniformEXT(mat.aoIdx)], finalUV).r : 1.0;

    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic  = vec4(albedo, metallic);
    gHeightAO        = vec4(height, ao, 0.0, 1.0);
}