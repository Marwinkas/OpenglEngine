 #version 430 core
#extension GL_ARB_bindless_texture : require // КРИТИЧЕСКИ ВАЖНО!
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec3 NormalOut;
layout (location = 2) out vec3 PositionOut;
in vec3 crntPos;
in vec3 Normal;
in vec2 texCoord;
in mat3 TBN;
uniform vec2 screenSize;
// --- Uniforms для света ---
uniform samplerCube shadowCubeMap;
uniform vec3 lightPos;      // Позиция лампочки
uniform vec4 lightColor;    // Цвет лампочки
uniform float farPlane;
uniform vec3 sunDir = vec3(0.0, -0.5, 0.0);       // Направление солнца (например, vec3(-1.0, -1.0, -1.0))
uniform vec4 sunColor;      // Цвет и яркость солнца
uniform vec3 camPos;
uniform float time;
uniform sampler2D sunShadowMap;
uniform mat4 sunLightSpaceMatrix;
// --- Текстуры материалов ---
uniform float heightScale = 0.02;
uniform float normalStrength = 1.0;
uniform sampler2D noiseTexture;
uniform float noiseScale = 0.1;
// Инпуты для параллакса
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
    // Новое для теней:
    int shadowSlot;
    mat4 lightSpaceMatrix;
};
// --- НАШ РАСШИРЕННЫЙ КОНТЕЙНЕР СО СВЕТОМ ---
struct PointLightData {
    vec4 posType;           // xyz = позиция, w = тип (0, 1, 2, 3, 4)
    vec4 colorInt;          // xyz = цвет, w = интенсивность
    vec4 dirRadius;         // xyz = направление, w = радиус
    vec4 shadowParams;      // x = innerCone, y = outerCone, z = castShadows, w = shadowSlot
    mat4 lightSpaceMatrix;  // Матрица для теней (занимает ровно 4 вектора)
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
    vec2 padding; // Выравнивание до 16 байт
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
uniform mat4 view; // Матрица камеры
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
// -------------------- Тени и Параллакс --------------------
vec3 getRandomDirection(vec2 uv, int i) {
    vec2 noiseUV = uv * noiseScale + vec2(float(i));
    return normalize(texture(noiseTexture, noiseUV).rgb * 2.0 - 1.0);
}
float AtlasShadowCalculation(vec3 fragPos, vec3 N, vec3 L, Light light) {
    if (light.shadowSlot < 0) return 0.0; // Нет слота = нет тени
    // Сдвиг по нормали для устранения "зебры"
    vec3 normalOffset = N * 0.02;
    vec4 fragPosLightSpace = light.lightSpaceMatrix * vec4(fragPos + normalOffset, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5; // Переводим в [0, 1]
    // Если точка за пределами света, тени нет
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
    float currentDepth = projCoords.z;
    float bias = max(0.001 * (1.0 - dot(N, L)), 0.0001);
    // --- МАГИЯ АТЛАСА ---
    // Узнаем, в каком квадратике мы находимся
    float gridCount = atlasResolution / tileSize;
    float gridX = mod(float(light.shadowSlot), gridCount);
    float gridY = floor(float(light.shadowSlot) / gridCount);
    // Масштабируем UV-координаты [0, 1] до размеров одного квадратика
float uvScale = tileSize / atlasResolution; 
    vec2 atlasUV = projCoords.xy * uvScale + vec2(gridX, gridY) * uvScale;
    // --- ГРАНИЦЫ КВАДРАТИКА ---
    vec2 tileMin = vec2(gridX, gridY) * uvScale;
    vec2 tileMax = tileMin + vec2(uvScale) - vec2(1.0 / atlasResolution);
    // PCF (Мягкие тени)
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / atlasResolution);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 sampleUV = atlasUV + vec2(x, y) * texelSize;
            sampleUV = clamp(sampleUV, tileMin, tileMax); // ЗАЩИТА
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
vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
// -------------------- ГЛАВНАЯ ФУНКЦИЯ СВЕТА --------------------
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
float SunShadowCalculation(vec3 N, vec3 L) {
    // 1. Сдвигаем позицию чуть-чуть вдоль нормали, чтобы избежать "зебры"
    // Это и есть Normal Bias. Он "прижимает" тень к ногам/стенам.
    vec3 normalOffset = N * 0.02; 
    vec4 fragPosLightSpace = sunLightSpaceMatrix * vec4(crntPos + normalOffset, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if(projCoords.z > 1.0) return 0.0;
    float currentDepth = projCoords.z;
    // 2. Делаем bias ОЧЕНЬ маленьким. Благодаря смещению по нормали выше, 
    // нам больше не нужны огромные числа здесь.
    float bias = max(0.001 * (1.0 - dot(N, L)), 0.0001); 
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(sunShadowMap, 0);
    // PCF для мягкости
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sunShadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    return shadow / 9.0;
}
float ShadowCalculation(vec3 fragPos, vec3 lightPos, vec3 N) {
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);
    float bias = max(0.05 * (1.0 - dot(N, normalize(-fragToLight))), 0.005);
    int samples = 4;
    float diskRadius = 0.02;
    float shadow = 0.0;
    for (int i = 0; i < samples; ++i) {
        float closestDepth = texture(shadowCubeMap, fragToLight + getRandomDirection(texCoord, i) * diskRadius).r * farPlane;
        if (currentDepth - bias > closestDepth) shadow += 1.0;
    }
    return shadow / float(samples);
}
float PointShadowCalculation_Atlas(vec3 fragPos, vec3 N, Light light) {
    if (light.shadowSlot < 0) return 0.0;
    vec3 fragToLight = fragPos - light.position;
    vec3 absVec = abs(fragToLight);
    // Ищем самую длинную ось (чтобы понять, на какую грань куба мы смотрим)
    float maxAxis = max(max(absVec.x, absVec.y), absVec.z);
    int face = 0;
    vec2 uv = vec2(0.0);
    // Переводим 3D вектор в 2D координаты для нужной стороны
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
    // Нормализуем координаты от [-maxAxis, maxAxis] к [0, 1]
    uv = 0.5 * (uv / maxAxis) + 0.5;
    // Считаем номер квадратика в Атласе
    int slot = light.shadowSlot + face;
    float gridCount = atlasResolution / tileSize;
    float gridX = mod(float(slot), gridCount);
    float gridY = floor(float(slot) / gridCount);
    float uvScale = tileSize / atlasResolution; 
    vec2 atlasUV = uv * uvScale + vec2(gridX, gridY) * uvScale;
    // Восстанавливаем глубину для 90-градусной перспективы
    float far = light.radius;
    float near = 0.1;
    float depthNDC = (far + near) / (far - near) - (2.0 * far * near) / (maxAxis * (far - near));
    float currentDepth = depthNDC * 0.5 + 0.5;
    // Смягчаем тени (PCF)
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / atlasResolution);
    vec3 L = normalize(-fragToLight);
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    // --- МАГИЯ: ВЫЧИСЛЯЕМ ГРАНИЦЫ КВАДРАТИКА ---
    vec2 tileMin = vec2(gridX, gridY) * uvScale;
    vec2 tileMax = tileMin + vec2(uvScale) - texelSize; // Отступаем 1 пиксель от края, чтобы точно не вылезти
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 sampleUV = atlasUV + vec2(x, y) * texelSize;
            // ЖЕСТКО ЗАПРЕЩАЕМ ВЫХОДИТЬ ЗА ГРАНИЦЫ!
            sampleUV = clamp(sampleUV, tileMin, tileMax); 
            float pcfDepth = texture(shadowAtlas, sampleUV).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    return shadow / 9.0;
}
void main() {
    MaterialData mat = materials[materialID];
    // 1. Подготовка нормалей и текстур
    vec3 viewDirTangent = normalize(TangentViewPos - TangentFragPos);
    // Защита от деления на ноль в параллаксе!
    vec2 uv = texCoord;
    if (mat.hasAlbedo == 1 && abs(viewDirTangent.z) > 0.001) {
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
    // =============================================================
    // 2. ВЫЧИСЛЯЕМ ИНДЕКС КЛАСТЕРА ДЛЯ ТЕКУЩЕГО ПИКСЕЛЯ
    // =============================================================
    // Получаем позицию пикселя в пространстве камеры, чтобы узнать Z
    vec4 viewPos = view * vec4(crntPos, 1.0);
    float zView = -viewPos.z; // В OpenGL камера смотрит в -Z, делаем положительным
    // Находим X и Y кластера (делим экран на 16x9 плиток)
    uint clusterX = uint(gl_FragCoord.x / (screenSize.x / float(gridDimX)));
    uint clusterY = uint(gl_FragCoord.y / (screenSize.y / float(gridDimY)));
    // Находим Z слой кластера (используем ту же логарифмическую формулу)
    uint clusterZ = uint(max(0.0, log(zView / zNear) * float(gridDimZ) / log(zFar / zNear)));
    // Финальный индекс в одномерном массиве SSBO
    uint clusterIdx = clusterX + 
                      clusterY * gridDimX + 
                      clusterZ * gridDimX * gridDimY;
    // Читаем, сколько ламп в нашем кубике
    uint offset = lightGrid[clusterIdx].offset;
    uint lightsInCluster = lightGrid[clusterIdx].count;
    // =============================================================
    // 2. ГЛАВНЫЙ ЦИКЛ СВЕТА (Собираем свет от 99 лампочек)
    // =============================================================
    for (uint i = 0; i < lightsInCluster; ++i) { 
        uint lightIndex = globalIndexList[offset + i];
        PointLightData rawLight = lights[lightIndex];
        // --- РАСПАКОВКА ИЗ UBO В ТВОЙ ФОРМАТ ---
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
        // ---------------------------------------
        vec3 L = vec3(0.0);
        float attenuation = 1.0;
        vec3 radiance = light.color * light.intensity;
        // --- DIRECTIONAL СВЕТ (Солнце) ---
        if (light.type == 0) {
            L = normalize(-light.direction);
            if (light.castShadows == 1) {
                float shadow = AtlasShadowCalculation(crntPos, N, L, light);
                radiance *= (1.0 - shadow);
            }
        }
        // --- POINT СВЕТ (Лампочка) ---
        else if (light.type == 1) {
            L = normalize(light.position - crntPos);
            float dist = length(light.position - crntPos);
            float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 0.001);
            // Включаем наши новые тени!
            if (light.castShadows == 1) {
                float shadow = PointShadowCalculation_Atlas(crntPos, N, light);
                attenuation *= (1.0 - shadow);
            }
        }
        // --- SPOT СВЕТ (Фонарик) ---
        else if (light.type == 2) {
            L = normalize(light.position - crntPos);
            float dist = length(light.position - crntPos);
            float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 0.001);
            float theta = dot(L, normalize(-light.direction));
            float epsilon = light.innerCone - light.outerCone;
            float spotEffect = clamp((theta - light.outerCone) / epsilon, 0.0, 1.0);
            attenuation *= spotEffect;
            // ТЕНИ ФОНАРИКА В АТЛАСЕ!
            if (light.castShadows == 1) {
                float shadow = AtlasShadowCalculation(crntPos, N, L, light);
                attenuation *= (1.0 - shadow);
            }
        }
        // --- RECT СВЕТ (Софтбокс / Площадной свет) ---
        else if (light.type == 3) {
            vec3 forward = normalize(-light.direction); // Куда смотрит плоскость
            vec3 toLight = light.position - crntPos;
            float dist = length(toLight);
            L = toLight / dist;
            float angleDot = dot(L, forward);
            // Свет излучается только с передней части плоскости
            if (angleDot > 0.0) { 
                float falloff = clamp(1.0 - (dist * dist) / (light.radius * light.radius), 0.0, 1.0);
                attenuation = (falloff * falloff) / (dist * dist + 0.001);
                // Смягчаем края, чтобы свет не обрывался резко
                attenuation *= smoothstep(0.0, 0.2, angleDot); 
            } else {
                attenuation = 0.0;
            }
        }
        // --- SKY СВЕТ (Небесное глобальное освещение) ---
        else if (light.type == 4) {
            // Чем больше нормаль пикселя смотрит вверх (Y = 1), тем он светлее
            float skyBlend = 0.5 * (N.y + 1.0); 
            vec3 skyRadiance = light.color * light.intensity * skyBlend;
            // Добавляем отражение от земли (делаем цвет земли темнее неба)
            vec3 groundColor = light.color * 0.2; 
            vec3 groundRadiance = groundColor * light.intensity * (1.0 - skyBlend);
            // Sky Light заполняет тени, поэтому мы прибавляем его прямо к ambient!
            // Ему не нужны блики specular, так как у неба нет одной точки
            ambient += (skyRadiance + groundRadiance) * albedo * ao;
            continue; // Пропускаем стандартный PBR расчет (CalculatePBR), идём к следующей лампочке
        }
        if (attenuation <= 0.0) continue;
        radiance *= attenuation;
        resultRadiance += CalculatePBR(N, V, L, albedo, metallic, roughness, radiance);
    }
    vec3 finalColor = ambient + resultRadiance;
    // Отдаем чистый, сырой HDR свет в наш G-Buffer для дальнейшей обработки
    FragColor = vec4(finalColor, 1.0);
} 