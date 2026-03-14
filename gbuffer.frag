#version 430 core
#extension GL_ARB_bindless_texture : require 

layout (location = 0) out vec4 gNormalRoughness;
layout (location = 1) out vec4 gAlbedoMetallic;

in vec3 crntPos;
in vec3 Normal;
in vec2 texCoord;
in mat3 TBN;

struct MaterialData {
    sampler2D albedoHandle;
    sampler2D normalHandle;
    sampler2D heightHandle;
    sampler2D metallicHandle;
    sampler2D roughnessHandle;
    sampler2D aoHandle;
    int hasAlbedo;
    int hasNormal;
    int hasHeight;
    int hasMetallic;
    int hasRoughness;
    int hasAO;
    int useTriplanar; 
    float triplanarScale; 
    
    // ВАЖНО: vec4 весит ровно 16 байт, как и наш float padding[4] в C++!
    // Общий размер структуры в видеокарте теперь ровно 96 байт.
    vec4 padding; 
};

layout(std430, binding = 2) readonly buffer MaterialBlock {
    MaterialData materials[];
};

flat in uint matID;

void main() {
    MaterialData mat = materials[matID];
    
    // БАЗОВЫЕ ЗНАЧЕНИЯ (Если текстуры нет или она еще грузится)
    vec3 albedo = vec3(0.8);
    float metallic = 0.0;
    float roughness = 0.9;
    vec3 worldNormal = normalize(Normal);

    // ЧИТАЕМ ТЕКСТУРЫ ТОЛЬКО ЕСЛИ ОНИ ГОТОВЫ (has... == 1)
    if (mat.hasAlbedo == 1) {
        albedo = texture(mat.albedoHandle, texCoord).rgb;
    }
    
    if (mat.hasMetallic == 1) {
        metallic = texture(mat.metallicHandle, texCoord).r;
    }
    
    if (mat.hasRoughness == 1) {
        roughness = texture(mat.roughnessHandle, texCoord).r;
    }

    if (mat.hasNormal == 1) {
        vec3 rawNorm = texture(mat.normalHandle, texCoord).rgb;
        // Защита: если текстура еще не прогрузилась и отдает чистый черный (0,0,0)
        if (length(rawNorm) > 0.01) {
            vec3 tangentNormal = rawNorm * 2.0 - 1.0;
            worldNormal = normalize(TBN * tangentNormal);
        }
    }

    // ФИНАЛЬНАЯ БРОНЯ ОТ ЧЕРНЫХ ПЯТЕН:
    // Если какая-то математика выдала NaN (ошибку) или нулевой вектор
    if (isnan(worldNormal.x) || isnan(worldNormal.y) || isnan(worldNormal.z) || length(worldNormal) < 0.1) {
        worldNormal = normalize(Normal); // Откатываемся к родной нормали полигона!
    }
    // ЗАПИСЬ В G-BUFFER
    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic = vec4(albedo, metallic); 
}