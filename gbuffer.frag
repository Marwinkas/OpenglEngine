#version 430 core
#extension GL_ARB_bindless_texture : require 

layout (location = 0) out vec4 gPositionMetallic;
layout (location = 1) out vec4 gNormalRoughness;
layout (location = 2) out vec4 gAlbedoAO;

in vec3 crntPos;
in vec3 Normal;
in vec2 texCoord;
in mat3 TBN;
in vec3 TangentViewPos;
in vec3 TangentFragPos;

uniform vec3 camPos;
uniform float heightScale = 0.02;

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
    vec2 padding; 
};

// Блок объявляем строго ОДИН раз
layout(std430, binding = 2) readonly buffer MaterialBlock {
    MaterialData materials[];
};

flat in uint matID;

// Функция для цвета (Albedo, Metallic, Roughness)
vec4 SampleTriplanar(sampler2D tex, vec3 p, vec3 n, float s) {
    vec3 blending = abs(n);
    blending = max((blending - 0.2) * 7.0, 0.0); 
    blending /= (blending.x + blending.y + blending.z); 

    vec4 x = texture(tex, p.zy * s);
    vec4 y = texture(tex, p.xz * s);
    vec4 z = texture(tex, p.xy * s);

    return x * blending.x + y * blending.y + z * blending.z;
}

// Специальная функция для Нормалей (учитывает направление граней)
vec3 TriplanarNormal(sampler2D tex, vec3 p, vec3 n, float s) {
    vec3 tX = texture(tex, p.zy * s).xyz * 2.0 - 1.0;
    vec3 tY = texture(tex, p.xz * s).xyz * 2.0 - 1.0;
    vec3 tZ = texture(tex, p.xy * s).xyz * 2.0 - 1.0;

    tX.y = -tX.y; tY.y = -tY.y; tZ.y = -tZ.y;

    tX.z *= sign(n.x);
    tY.z *= sign(n.y);
    tZ.z *= sign(n.z);

    vec3 blending = abs(n);
    blending = max((blending - 0.2) * 7.0, 0.0);
    blending /= (blending.x + blending.y + blending.z);

    vec3 blendedTangentNormal = tX * blending.x + tY * blending.y + tZ * blending.z;
    
    return normalize(vec3(n.xy + blendedTangentNormal.xy, n.z));
}

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir) {
    MaterialData mat = materials[matID];
    
    float minLayers = 8.0;
    float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, max(dot(vec3(0.0, 0.0, 1.0), viewDir), 0.0));  

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    
    vec2 P = viewDir.xy / viewDir.z * heightScale; 
    vec2 deltaTexCoords = P / numLayers;
    
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = texture(mat.heightHandle, currentTexCoords).r;
    
    while(currentLayerDepth < currentDepthMapValue) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = texture(mat.heightHandle, currentTexCoords).r;  
        currentLayerDepth += layerDepth;  
    }
    
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = texture(mat.heightHandle, prevTexCoords).r - currentLayerDepth + layerDepth;
    
    float weight = afterDepth / (afterDepth - beforeDepth);
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}

void main() {
    MaterialData mat = materials[matID];
    
    vec3 V = normalize(camPos - crntPos);
    vec3 N = normalize(Normal);
    vec2 finalUV = texCoord;
    
    vec3 albedo = vec3(0.8);
    float metallic = 0.0, roughness = 0.9, ao = 1.0;
    vec3 worldNormal = N;

    if (mat.useTriplanar == 1) {
        float scale = mat.triplanarScale;
        albedo = (mat.hasAlbedo == 1) ? SampleTriplanar(mat.albedoHandle, crntPos, N, scale).rgb : vec3(0.8);
       
        metallic = (mat.hasMetallic == 1) ? SampleTriplanar(mat.metallicHandle, crntPos, N, scale).r : 0.0;
        roughness = (mat.hasRoughness == 1) ? SampleTriplanar(mat.roughnessHandle, crntPos, N, scale).r : 0.9;
        ao = (mat.hasAO == 1) ? SampleTriplanar(mat.aoHandle, crntPos, N, scale).r : 1.0;
        if (mat.hasNormal == 1) worldNormal = TriplanarNormal(mat.normalHandle, crntPos, N, scale);
    } 
    else {

        albedo = (mat.hasAlbedo == 1) ? texture(mat.albedoHandle, finalUV).rgb : vec3(0.8);
        metallic = (mat.hasMetallic == 1) ? texture(mat.metallicHandle, finalUV).r : 0.0;
        roughness = (mat.hasRoughness == 1) ? texture(mat.roughnessHandle, finalUV).r : 0.9;
        ao = (mat.hasAO == 1) ? texture(mat.aoHandle, finalUV).r : 1.0;

        if(mat.hasNormal == 1) {
            vec3 tangentNormal = texture(mat.normalHandle, finalUV).rgb * 2.0 - 1.0;
            // tangentNormal.y = -tangentNormal.y; // Раскомментируй для DirectX нормалей
            worldNormal = normalize(TBN * normalize(tangentNormal));
        }
    }

    // Сохраняем в G-Buffer
    gPositionMetallic = vec4(crntPos, metallic);
    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoAO = vec4(albedo, ao);
}