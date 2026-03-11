#version 430 core
#extension GL_ARB_bindless_texture : require 

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec3 NormalOut;
layout (location = 2) out vec3 PositionOut;

in vec3 crntPos;
in vec3 Normal;
in vec2 texCoord;
in mat3 TBN;

uniform vec2 screenSize;
uniform samplerCube shadowCubeMap;
uniform vec3 lightPos;      
uniform vec4 lightColor;    
uniform float farPlane;
uniform vec3 sunDir = vec3(0.0, -0.5, 0.0);        
uniform vec4 sunColor;      
uniform vec3 camPos;
uniform float time;
uniform sampler2DArray sunShadowMap; 
uniform mat4 sunLightSpaceMatrices[4]; 
uniform float cascadeSplits[4]; 
uniform float heightScale = 0.02;
uniform float normalStrength = 1.0;

uniform sampler2D noiseTexture; // Сюда теперь будет приходить наш красивый голубой шум
uniform float noiseScale = 0.1;

in vec3 TangentViewPos;
in vec3 TangentFragPos;

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float radius;
};

#define MAX_LIGHTS 16

struct Light {
    int type; 
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float radius;
    float innerCone;
    float outerCone;
    int castShadows;
    int shadowSlot;
    mat4 lightSpaceMatrix;
};

struct PointLightData {
    vec4 posType;                
    vec4 colorInt;              
    vec4 dirRadius;             
    vec4 shadowParams;          
    mat4 lightSpaceMatrix;  
};

layout(std140) uniform LightBlock {
    int activeLightsCount;
    PointLightData lights[100];
};

uniform sampler2D shadowAtlas;
uniform float atlasResolution;
uniform float tileSize;

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
    vec2 padding; 
};

layout(std430, binding = 2) readonly buffer MaterialBlock {
    MaterialData materials[];
};

uniform int materialID;

struct LightGrid {
    uint offset;
    uint count;
};

layout(std430, binding = 4) readonly buffer GridBuffer { LightGrid lightGrid[]; };
layout(std430, binding = 5) readonly buffer IndexBuffer { uint globalIndexList[]; };

uniform uint gridDimX = 16;
uniform uint gridDimY = 9;
uniform uint gridDimZ = 24;
uniform float zNear = 0.1;
uniform float zFar = 1000.0;
uniform mat4 view; 
uniform float lightSize = 20.0;

const vec2 poissonDisk[16] = vec2[](
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ),
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 )
);

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    return a2 / (3.141592 * pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0));
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- УНИВЕРСАЛЬНЫЙ PCSS ДЛЯ POINT И SPOT ИСТОЧНИКОВ ---
float CalculatePCSS_Atlas(vec2 atlasUV, vec2 tileMin, vec2 tileMax, float currentDepth, float bias, mat2 rot) {
    vec2 texelSize = vec2(1.0 / atlasResolution);
    
    // Широкий поиск
    float searchRadius = lightSize * 3.0;

    int blockers = 0;
    float avgBlockerDepth = 0.0;

    float centerDepth = texture(shadowAtlas, clamp(atlasUV, tileMin, tileMax)).r;
    if (centerDepth < currentDepth - bias) {
        blockers++;
        avgBlockerDepth += centerDepth;
    }

    for (int i = 0; i < 8; i++) {
        vec2 offset = rot * poissonDisk[i] * searchRadius * texelSize;
        float pcfDepth = texture(shadowAtlas, clamp(atlasUV + offset, tileMin, tileMax)).r;
        if (pcfDepth < currentDepth - bias) {
            blockers++;
            avgBlockerDepth += pcfDepth;
        }
    }

    if (blockers == 0) return 0.0; 
    avgBlockerDepth /= float(blockers);

    // Умножаем на ОГРОМНОЕ число (400.0), чтобы компенсировать сжатие Z-буфера!
    float penumbra = (currentDepth - avgBlockerDepth) * 400.0;
    
    // Даем размыться до 20 пикселей текстуры атласа
    float filterRadius = clamp(penumbra * lightSize, 0.0, 20.0);

    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rot * poissonDisk[i] * filterRadius * texelSize; 
        float pcfDepth = texture(shadowAtlas, clamp(atlasUV + offset, tileMin, tileMax)).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

    return shadow / 16.0;
}

float AtlasShadowCalculation(vec3 fragPos, vec3 N, vec3 L, Light light) {
    if (light.shadowSlot < 0) return 0.0;         

    // Умный отступ (Slope-Scaled Bias): чем сильнее свет скользит по поверхности, тем больше отступ
    float slopeScale = clamp(1.0 - max(dot(N, L), 0.0), 0.0, 1.0);
    vec3 normalOffset = N * (0.01 + 0.02 * slopeScale); 
    
    vec4 fragPosLightSpace = light.lightSpaceMatrix * vec4(fragPos + normalOffset, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;         
    
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;
    
    float currentDepth = projCoords.z;
    float bias = 0.0001 + 0.0005 * slopeScale; // Базовый отступ + добавка от наклона
    
    float gridCount = atlasResolution / tileSize;
    float gridX = mod(float(light.shadowSlot), gridCount);
    float gridY = floor(float(light.shadowSlot) / gridCount);
    float uvScale = tileSize / atlasResolution; 
    
    vec2 atlasUV = projCoords.xy * uvScale + vec2(gridX, gridY) * uvScale;
    vec2 tileMin = vec2(gridX, gridY) * uvScale;
    vec2 tileMax = tileMin + vec2(uvScale) - vec2(1.0 / atlasResolution);
    
    // Магия голубого шума: привязываем его к координатам экрана (gl_FragCoord)
    vec2 noiseUV = (gl_FragCoord.xy / 256.0) + vec2(float(light.shadowSlot) * 0.137);
    float randomAngle = texture(noiseTexture, noiseUV).r * 6.2831853;
    float s = sin(randomAngle);
    float c = cos(randomAngle);
    mat2 rot = mat2(c, -s, s, c);
    
    return CalculatePCSS_Atlas(atlasUV, tileMin, tileMax, currentDepth, bias, rot);
}

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir) { 
    MaterialData mat = materials[materialID];
    float height = texture(mat.heightHandle, texCoords).r;
    return texCoords - (viewDir.xy / viewDir.z) * (height * heightScale);
}

vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 CalculatePBR(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 radiance) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = numerator / denominator;
    return (kD * albedo / 3.141592 + specular) * radiance * max(dot(N, L), 0.0);
}

float SearchBlocker(vec3 projCoords, int layer, float zReceiver, float bias, vec2 texelSize, mat2 rot) {
    int blockers = 0;
    float avgBlockerDepth = 0.0;
    
    float searchRadius = lightSize * 3.0; 

    float centerDepth = texture(sunShadowMap, vec3(projCoords.xy, layer)).r;
    if (centerDepth < zReceiver - bias) {
        blockers++;
        avgBlockerDepth += centerDepth;
    }

    for (int i = 0; i < 8; i++) {
        vec2 offset = rot * poissonDisk[i] * searchRadius;
        float pcfDepth = texture(sunShadowMap, vec3(projCoords.xy + offset * texelSize, layer)).r;
        
        if (pcfDepth < zReceiver - bias) {
            blockers++;
            avgBlockerDepth += pcfDepth;
        }
    }
    
    if (blockers > 0) return avgBlockerDepth / float(blockers);
    return -1.0; 
}

float CalculateSingleCascadeShadow(vec3 fragPos, vec3 N, vec3 L, int layer, float depth) {
    // Умный отступ для солнца (Slope-Scaled Bias)
    float slopeScale = clamp(1.0 - max(dot(N, L), 0.0), 0.0, 1.0);
    vec3 normalOffset = N * ((0.0005 + 0.001 * slopeScale) * (layer + 1.0));
    
    vec4 fragPosLightSpace = sunLightSpaceMatrices[layer] * vec4(fragPos + normalOffset, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if (projCoords.z > 1.0) return 0.0;
    
    vec2 texelSize = 1.0 / vec2(textureSize(sunShadowMap, 0));
    float currentDepth = projCoords.z;
    float bias = (0.0001 + 0.0004 * slopeScale) * (layer + 1.0);
    
    // Голубой шум на координатах экрана (чтобы точки не слипались)
    vec2 noiseUV = (gl_FragCoord.xy / 256.0) + vec2(float(layer) * 0.137);
    float randomAngle = texture(noiseTexture, noiseUV).r * 6.2831853;
    float s = sin(randomAngle);
    float c = cos(randomAngle);
    mat2 rot = mat2(c, -s, s, c); 
    
    float avgBlockerDepth = SearchBlocker(projCoords, layer, currentDepth, bias, texelSize, rot);
    if (avgBlockerDepth == -1.0) return 0.0; 
    
    float penumbra = (currentDepth - avgBlockerDepth) * 150.0; 
    float filterRadius = clamp(penumbra * lightSize, 0.0, 20.0);
    
    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rot * poissonDisk[i] * filterRadius;
        float pcfDepth = texture(sunShadowMap, vec3(projCoords.xy + offset * texelSize, layer)).r; 
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }
    
    return shadow / 16.0;
}

// Главная функция с Cascade Blending
float SunShadowCalculation(vec3 fragPos, vec3 N, vec3 L) {
    float depth = abs((view * vec4(fragPos, 1.0)).z);
    int layer = 3;
    
    for(int i = 0; i < 4; i++) {
        if(depth < cascadeSplits[i]) {
            layer = i;
            break;
        }
    }
    
    float shadow = CalculateSingleCascadeShadow(fragPos, N, L, layer, depth);
    
    if (layer < 3) {
        float splitDist = cascadeSplits[layer];
        float blendBand = splitDist * 0.15; 
        float distToSplit = splitDist - depth;
        
        if (distToSplit < blendBand) {
            float nextShadow = CalculateSingleCascadeShadow(fragPos, N, L, layer + 1, depth);
            float blendFactor = smoothstep(0.0, blendBand, blendBand - distToSplit);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }
    
    return shadow;
}

float PointShadowCalculation_Atlas(vec3 fragPos, vec3 N, Light light) {
    if (light.shadowSlot < 0) return 0.0;
    vec3 fragToLight = fragPos - light.position;
    vec3 absVec = abs(fragToLight);
    
    float maxAxis = max(max(absVec.x, absVec.y), absVec.z);
    int face = 0;
    vec2 uv = vec2(0.0);
    
    if (maxAxis == absVec.x) {
        face = fragToLight.x > 0.0 ? 0 : 1;
        uv = fragToLight.x > 0.0 ? vec2(-fragToLight.z, -fragToLight.y) : vec2(fragToLight.z, -fragToLight.y);
    } else if (maxAxis == absVec.y) {
        face = fragToLight.y > 0.0 ? 2 : 3;
        uv = fragToLight.y > 0.0 ? vec2(fragToLight.x, fragToLight.z) : vec2(fragToLight.x, -fragToLight.z);
    } else {
        face = fragToLight.z > 0.0 ? 4 : 5;
        uv = fragToLight.z > 0.0 ? vec2(fragToLight.x, -fragToLight.y) : vec2(-fragToLight.x, -fragToLight.y);
    }
    
    uv = 0.5 * (uv / maxAxis) + 0.5;
    int slot = light.shadowSlot + face;
    
    float gridCount = atlasResolution / tileSize;
    float gridX = mod(float(slot), gridCount);
    float gridY = floor(float(slot) / gridCount);
    float uvScale = tileSize / atlasResolution; 
    vec2 atlasUV = uv * uvScale + vec2(gridX, gridY) * uvScale;
    
    float far = light.radius;
    float near = 0.1;
    float depthNDC = (far + near) / (far - near) - (2.0 * far * near) / (maxAxis * (far - near));
    float currentDepth = depthNDC * 0.5 + 0.5;
    
    vec3 L = normalize(-fragToLight);
    
    // Умный отступ (Slope-Scaled Bias) для лампочек
    float slopeScale = clamp(1.0 - max(dot(N, L), 0.0), 0.0, 1.0);
    float bias = 0.0001 + 0.0005 * slopeScale;
    
    vec2 tileMin = vec2(gridX, gridY) * uvScale;
    vec2 tileMax = tileMin + vec2(uvScale) - vec2(1.0 / atlasResolution);  
    
    // Экранный голубой шум
    vec2 noiseUV = (gl_FragCoord.xy / 256.0) + vec2(float(slot) * 0.137);
    float randomAngle = texture(noiseTexture, noiseUV).r * 6.2831853;
    float s = sin(randomAngle);
    float c = cos(randomAngle);
    mat2 rot = mat2(c, -s, s, c);
    
    return CalculatePCSS_Atlas(atlasUV, tileMin, tileMax, currentDepth, bias, rot);
}

void main() {
    MaterialData mat = materials[materialID];
    vec3 viewDirTangent = normalize(TangentViewPos - TangentFragPos);
    vec2 uv = texCoord;
    
    if (mat.hasHeight == 1 && abs(viewDirTangent.z) > 0.001) {
        uv = ParallaxMapping(texCoord, viewDirTangent);
    }

    uv = clamp(uv, 0.0, 1.0);
    vec3 N = normalize(Normal);
    
    if(mat.hasNormal == 1) {
        vec3 tangentNormal = texture(mat.normalHandle, uv).rgb * 2.0 - 1.0;
        tangentNormal.xy *= normalStrength;
        N = normalize(TBN * normalize(tangentNormal));
    }
    
    NormalOut = N;
    PositionOut = crntPos;
    vec3 albedo = (mat.hasAlbedo == 1) ? texture(mat.albedoHandle, uv).rgb : vec3(0.8);

    float metallic = (mat.hasMetallic == 1) ? texture(mat.metallicHandle, uv).r : 0.0;
    float roughness = (mat.hasRoughness == 1) ? texture(mat.roughnessHandle, uv).r : 0.9;
    float ao = (mat.hasAO == 1) ? texture(mat.aoHandle, uv).r : 1.0;
    vec3 V = normalize(camPos - crntPos);
    vec3 resultRadiance = vec3(0.0);
    vec3 ambient = vec3(0.005) * albedo * ao;
    
    vec4 viewPos = view * vec4(crntPos, 1.0);
    float zView = -viewPos.z;         
    uint clusterX = uint(gl_FragCoord.x / (screenSize.x / float(gridDimX)));
    uint clusterY = uint(gl_FragCoord.y / (screenSize.y / float(gridDimY)));
    uint clusterZ = uint(max(0.0, log(zView / zNear) * float(gridDimZ) / log(zFar / zNear)));
    uint clusterIdx = clusterX + clusterY * gridDimX + clusterZ * gridDimX * gridDimY;
    
    uint offset = lightGrid[clusterIdx].offset;
    uint lightsInCluster = lightGrid[clusterIdx].count;
    
    for (uint i = 0; i < lightsInCluster; ++i) { 
        uint lightIndex = globalIndexList[offset + i];
        PointLightData rawLight = lights[lightIndex];
        
        Light light;
        light.type = int(rawLight.posType.w);
        light.position = rawLight.posType.xyz;
        light.color = rawLight.colorInt.xyz;
        light.intensity = rawLight.colorInt.w;
        light.direction = rawLight.dirRadius.xyz;
        light.radius = rawLight.dirRadius.w;
        light.innerCone = rawLight.shadowParams.x;
        light.outerCone = rawLight.shadowParams.y;
        light.castShadows = int(rawLight.shadowParams.z);
        light.shadowSlot = int(rawLight.shadowParams.w);
        light.lightSpaceMatrix = rawLight.lightSpaceMatrix;
        
        vec3 L = vec3(0.0);
        float attenuation = 1.0;
        vec3 radiance = light.color * light.intensity;
        
        if (light.type == 0) {
            L = normalize(-light.direction);
            if (light.castShadows == 1) {
                float shadow = SunShadowCalculation(crntPos, N, L);
                radiance *= (1.0 - shadow);
            }
        }
        else if (light.type == 1) {
            L = normalize(light.position - crntPos);
            float dist = length(light.position - crntPos);
            float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 0.001);
            if (light.castShadows == 1) {
                float shadow = PointShadowCalculation_Atlas(crntPos, N, light);
                attenuation *= (1.0 - shadow);
            }
        }
        else if (light.type == 2) {
            L = normalize(light.position - crntPos);
            float dist = length(light.position - crntPos);
            float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 0.001);
            float theta = dot(L, normalize(-light.direction));
            float epsilon = light.innerCone - light.outerCone;
            float spotEffect = clamp((theta - light.outerCone) / epsilon, 0.0, 1.0);
            attenuation *= spotEffect;
            if (light.castShadows == 1) {
                float shadow = AtlasShadowCalculation(crntPos, N, L, light);
                attenuation *= (1.0 - shadow);
            }
        }
        else if (light.type == 3) {
            vec3 forward = normalize(-light.direction);             
            vec3 toLight = light.position - crntPos;
            float dist = length(toLight);
            L = toLight / dist;
            float angleDot = dot(L, forward);
            if (angleDot > 0.0) { 
                float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
                attenuation = (falloff * falloff) / (dist * dist + 0.001);
                attenuation *= smoothstep(0.0, 0.2, angleDot); 
            } else {
                attenuation = 0.0;
            }
        }
        else if (light.type == 4) {
            float skyBlend = 0.5 * (N.y + 1.0); 
            vec3 skyRadiance = light.color * light.intensity * skyBlend;
            vec3 groundColor = light.color * 0.2; 
            vec3 groundRadiance = groundColor * light.intensity * (1.0 - skyBlend);
            ambient += (skyRadiance + groundRadiance) * albedo * ao;
            continue;         
        }
        
        if (attenuation <= 0.0) continue;
        radiance *= attenuation;
        resultRadiance += CalculatePBR(N, V, L, albedo, metallic, roughness, radiance);
    }
    
    vec3 finalColor = ambient + resultRadiance;
    FragColor = vec4(finalColor, 1.0);
}