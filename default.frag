#version 430 core
#extension GL_ARB_bindless_texture : require // КРИТИЧЕСКИ ВАЖНО!

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec3 NormalOut;
layout (location = 2) out vec3 PositionOut;

in vec3 crntPos;
in vec3 Normal;
in vec2 texCoord;
in mat3 TBN;
// TangentViewPos и TangentFragPos больше не нужны, считаем правильно внутри!

// --- Uniforms для света ---
uniform samplerCube shadowCubeMap;
uniform float farPlane;

uniform vec3 camPos;
uniform float time;
uniform sampler2D sunShadowMap;
uniform mat4 sunLightSpaceMatrix;

// --- Текстуры материалов ---
uniform float heightScale = 0.02;
uniform float normalStrength = 1.0;
uniform sampler2D noiseTexture;
uniform float noiseScale = 0.1;

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

// --- НАШ РАСШИРЕННЫЙ КОНТЕЙНЕР СО СВЕТОМ ---
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

// -------------------- Вспомогательные функции PBR --------------------
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

vec3 getRandomDirection(vec2 uv, int i) {
    vec2 noiseUV = uv * noiseScale + vec2(float(i));
    return normalize(texture(noiseTexture, noiseUV).rgb * 2.0 - 1.0);
}

// -------------------- Тени и Параллакс --------------------
float AtlasShadowCalculation(vec3 fragPos, vec3 N, vec3 L, Light light) {
    // ЗАЩИТА ОТ NaN: если размер атласа 0 (еще не передан), отключаем тень
    if (light.shadowSlot < 0 || atlasResolution <= 0.0) return 0.0; 

    vec3 normalOffset = N * 0.02;
    vec4 fragPosLightSpace = light.lightSpaceMatrix * vec4(fragPos + normalOffset, 1.0);
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.001 * (1.0 - dot(N, L)), 0.0001);

    float gridCount = atlasResolution / tileSize;
    float gridX = mod(float(light.shadowSlot), gridCount);
    float gridY = floor(float(light.shadowSlot) / gridCount);

    float uvScale = tileSize / atlasResolution; 
    vec2 atlasUV = projCoords.xy * uvScale + vec2(gridX, gridY) * uvScale;

    vec2 tileMin = vec2(gridX, gridY) * uvScale;
    vec2 tileMax = tileMin + vec2(uvScale) - vec2(1.0 / atlasResolution);

    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / atlasResolution);
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 sampleUV = atlasUV + vec2(x, y) * texelSize;
            sampleUV = clamp(sampleUV, tileMin, tileMax);

            float pcfDepth = texture(shadowAtlas, sampleUV).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    return shadow / 9.0;
}

float PointShadowCalculation_Atlas(vec3 fragPos, vec3 N, Light light) {
    // ЗАЩИТА ОТ NaN
    if (light.shadowSlot < 0 || atlasResolution <= 0.0) return 0.0;
    
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
    
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / atlasResolution);
    vec3 L = normalize(-fragToLight);
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);

    vec2 tileMin = vec2(gridX, gridY) * uvScale;
    vec2 tileMax = tileMin + vec2(uvScale) - texelSize; 

    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 sampleUV = atlasUV + vec2(x, y) * texelSize;
            sampleUV = clamp(sampleUV, tileMin, tileMax); 
            
            float pcfDepth = texture(shadowAtlas, sampleUV).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    return shadow / 9.0;
}

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir) { 
    MaterialData mat = materials[materialID];
    float height = texture(mat.heightHandle, texCoords).r;
    return texCoords - (viewDir.xy / viewDir.z) * (height * heightScale);
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

void main() {
    MaterialData mat = materials[materialID];
    
    // 1. ИДЕАЛЬНАЯ МАТЕМАТИКА ПАРАЛЛАКСА
    // Считаем вектор взгляда в мире, а затем переводим его в касательное пространство (Tangent Space)
    vec3 viewDirWorld = normalize(camPos - crntPos);
    vec3 viewDirTangent = normalize(transpose(TBN) * viewDirWorld);
    
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

    // --- БЕЗОПАСНАЯ ВЫБОРКА ТЕКСТУР ---
    // Никаких тернарных операторов! Только надежные if, чтобы GPU не упал.
    vec3 albedo = vec3(0.8);
    if (mat.hasAlbedo == 1) albedo = texture(mat.albedoHandle, uv).rgb;

    float metallic = 0.0;
    if (mat.hasMetallic == 1) metallic = texture(mat.metallicHandle, uv).r;

    float roughness = 0.9;
    if (mat.hasRoughness == 1) roughness = texture(mat.roughnessHandle, uv).r;

    float ao = 1.0;
    if (mat.hasAO == 1) ao = texture(mat.aoHandle, uv).r;
    // ------------------------------------

    vec3 V = viewDirWorld;
    vec3 resultRadiance = vec3(0.0);
    vec3 ambient = vec3(0.005) * albedo * ao;

    // 2. ГЛАВНЫЙ ЦИКЛ СВЕТА
    for (int i = 0; i < activeLightsCount; ++i) { 
        PointLightData rawLight = lights[i];
        
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

        if (light.type == 0) { // DIRECTIONAL
            L = normalize(-light.direction);
            if (light.castShadows == 1) {
                float shadow = AtlasShadowCalculation(crntPos, N, L, light);
                radiance *= (1.0 - shadow);
            }
        }
        else if (light.type == 1) { // POINT
            L = normalize(light.position - crntPos);
            float dist = length(light.position - crntPos);
            float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 0.001);
            
            if (light.castShadows == 1) {
                float shadow = PointShadowCalculation_Atlas(crntPos, N, light);
                attenuation *= (1.0 - shadow);
            }
        }
        else if (light.type == 2) { // SPOT
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
        else if (light.type == 3) { // RECT
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
        else if (light.type == 4) { // SKY
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