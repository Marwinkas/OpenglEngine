#version 330 core

#define GOLDEN_ANGLE 2.39996323
out vec4 FragColor;
in vec2 texCoords;

// Текстуры G-буфера и эффектов
uniform sampler2D screenTexture;  // Основной свет
uniform sampler2D ssgiTexture;    // Непрямое освещение
uniform sampler2D normalTexture;
uniform sampler2D positionTexture;
uniform sampler2D ssaoTexture;    // Тени SSAO
uniform sampler2D bloomTexture;

// Параметры тумана
uniform bool enableFog;
uniform float fogDensity;
uniform float fogHeightFalloff;
uniform float fogBaseHeight;
uniform vec3 fogColor;
uniform vec3 inscatterColor;
uniform float inscatterPower;
uniform float inscatterIntensity;
uniform vec3 sunDirFog;

// Глобальные настройки
uniform float gamma;
uniform float contrast;
uniform float saturation;
uniform float temperature;

// Виньетка и Аберрация
uniform bool enableVignette;
uniform float vignetteIntensity;
uniform bool enableChromaticAberration;
uniform float caIntensity;

// Bloom & Flares
uniform bool enableBloom;
uniform float bloomIntensity;
uniform bool enableLensFlares;
uniform float flareIntensity;
uniform float ghostDispersal;
uniform int ghosts;

// Экспозиция
uniform float currentExposure;
uniform float exposureCompensation;
uniform int autoExposure;
uniform float minBrightness;
uniform float maxBrightness;

// Depth of Field
uniform bool enableDoF;
uniform float focusDistance;
uniform float focusRange;
uniform float bokehSize;
uniform vec3 camPos; 

// Motion Blur & God Rays
uniform float time;
uniform bool enableMotionBlur;
uniform float mbStrength;
uniform mat4 prevViewProj;

uniform bool enableGodRays;
uniform float godRaysIntensity;
uniform vec2 lightScreenPos;

// Доп. фильтры
uniform bool enableFilmGrain;
uniform float grainIntensity;
uniform bool enableSharpen;
uniform float sharpenIntensity;

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

float random(vec2 uv) {
    return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

vec3 KelvinToRGB(float temp) {
    if (temp < 1000.0) temp = 6500.0; 
    temp /= 100.0;
    float r, g, b;
    if (temp <= 66.0) {
        r = 255.0;
        g = 99.470802 * log(temp) - 161.119568;
        b = (temp <= 19.0) ? 0.0 : 138.517731 * log(temp - 10.0) - 305.044793;
    } else {
        r = 329.698727 * pow(temp - 60.0, -0.13320476);
        g = 288.12217 * pow(temp - 60.0, -0.07551485);
        b = 255.0;
    }
    return clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
}
vec3 sampleChromatic(sampler2D tex, vec2 uv) {
    if (!enableChromaticAberration) return texture(tex, uv).rgb;

    // Переводим UV в диапазон [-1, 1] относительно центра
    vec2 coords = uv * 2.0 - 1.0;
    
    // Вычисляем "выпуклость" линзы (Barrel Distortion)
    // k - коэффициент искривления. caIntensity влияет и на сдвиг, и на изгиб.
    float k = length(coords) * caIntensity * 2.0; 
    
    // Искажаем UV для каждого канала отдельно (Красный тянется сильнее всех)
    vec2 uvR = uv - coords * k * 0.02;
    vec2 uvG = uv; // Зеленый оставляем в центре
    vec2 uvB = uv + coords * k * 0.02;
    
    return vec3(
        texture(tex, uvR).r,
        texture(tex, uvG).g,
        texture(tex, uvB).b
    );
}
// Функция сборки полного освещения для одного пикселя
vec3 getFullLitColor(vec2 uv) {
    vec3 direct = sampleChromatic(screenTexture, uv);
    vec3 indirect = texture(ssgiTexture, uv).rgb;
    float ssao = texture(ssaoTexture, uv).r;
    return (direct + indirect * 2.0) * ssao;
}

void main() {
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    bool isSky = length(centerPos) < 0.01;
    vec3 finalSceneColor = vec3(0.0);

    // ==========================================
    // ЭТАП 1: ГЕОМЕТРИЯ (DoF, Motion Blur, Fog)
    // ==========================================
    if (isSky) {
        finalSceneColor = texture(screenTexture, texCoords).rgb;
    } 
    else {
        // 1.1 Собираем базовый цвет текущего пикселя (Свет + SSAO)
        vec3 currentPixelColor = getFullLitColor(texCoords);
        finalSceneColor = currentPixelColor;

        // 1.2 DEPTH OF FIELD (Размытие фона)
        if (enableDoF) {
            float distToCam = length(centerPos - camPos);
            float dofFactor = clamp((abs(distToCam - focusDistance) - focusRange) / max(bokehSize, 0.001), 0.0, 1.0);
            
            if (dofFactor > 0.0) {
                vec3 bokehColor = vec3(0.0);
                float rad = dofFactor * 0.015; 
                int samples = 16;
                for(int i = 0; i < samples; i++) {
                    float theta = float(i) * GOLDEN_ANGLE;
                    float r = sqrt(float(i) / float(samples)) * rad;
                    vec2 offset = vec2(sin(theta), cos(theta)) * r;
                    bokehColor += getFullLitColor(texCoords + offset);
                }
                finalSceneColor = mix(finalSceneColor, bokehColor / float(samples), dofFactor);
            }
        }

        // 1.3 MOTION BLUR (Размытие в движении)
        if (enableMotionBlur) {
            vec4 prevClip = prevViewProj * vec4(centerPos, 1.0);
            vec2 prevNDC = prevClip.xy / prevClip.w;
            vec2 prevUV = prevNDC * 0.5 + 0.5;
            
            vec2 velocity = (texCoords - prevUV) * mbStrength * 2.0; 
            velocity = clamp(velocity, vec2(-0.03), vec2(0.03)); 
            
            if (length(velocity) > 0.0001) {
                int mbSamples = 8;
                vec3 mbAccum = finalSceneColor;
                for(int i = 1; i < mbSamples; ++i) {
                    vec2 offset = velocity * (float(i) / float(mbSamples - 1) - 0.5);
                    mbAccum += getFullLitColor(texCoords + offset);
                }
                finalSceneColor = mbAccum / float(mbSamples);
            }
        }

        // 1.4 ТУМАН (Поверх размытия и SSAO)
        if (enableFog) {
            float distToCamFog = length(centerPos - camPos);
            float heightDensity = exp(-(centerPos.y - fogBaseHeight) * fogHeightFalloff);
            float fogFactor = clamp(1.0 - exp(-distToCamFog * fogDensity * heightDensity), 0.0, 1.0);
            
            vec3 viewDirFog = normalize(centerPos - camPos);
            float inscatter = pow(max(dot(viewDirFog, normalize(-sunDirFog)), 0.0), inscatterPower);
            vec3 currentFogColor = fogColor + (inscatterColor * inscatter * inscatterIntensity);
            
            finalSceneColor = mix(finalSceneColor, currentFogColor, fogFactor);
        }
    }

    vec3 finalColor = finalSceneColor;

    // ==========================================
    // ЭТАП 2: ГЛОБАЛЬНЫЕ ЭФФЕКТЫ (Bloom, God Rays)
    // ==========================================

    if (enableBloom) {
        finalColor += texture(bloomTexture, texCoords).rgb * bloomIntensity;
    }
    // --- 2.1 LENS FLARES (Fix & Polish) ---
    if (enableLensFlares) {
        // 1. Координаты, отраженные относительно центра экрана
        vec2 uv = texCoords;
        vec2 mirroredUV = vec2(1.0) - uv;
        
        // Вектор от отраженной точки к центру
        vec2 ghostDir = (vec2(0.5) - mirroredUV) * ghostDispersal;
        
        vec3 flareResult = vec3(0.0);
        
        // 2. Рисуем "Призраков" (Ghosts)
        for (int i = 0; i < ghosts; ++i) {
            // УБРАЛИ fract()! Теперь блики просто улетают за экран.
            vec2 offset = mirroredUV + ghostDir * float(i);
            
            // Плавное затухание у краев экрана (Vignette-like weight)
            // Если блик выходит за [0, 1], weight станет отрицательным или нулевым
            float weight = pow(1.0 - distance(offset, vec2(0.5)) / 0.707, 10.0);
            
            if (weight > 0.0) {
                // Добавляем микро-расслоение цвета (аберрация блика)
                float distortion = 0.005;
                float r = texture(bloomTexture, offset + normalize(ghostDir) * distortion).r;
                float g = texture(bloomTexture, offset).g;
                float b = texture(bloomTexture, offset - normalize(ghostDir) * distortion).b;
                
                flareResult += vec3(r, g, b) * weight;
            }
        }
        
        // 3. Добавляем "Halo" (Кольцо)
        // Кольцо обычно находится на фиксированном расстоянии от инвертированной точки
        vec2 haloVec = normalize(ghostDir) * ghostDispersal * 1.5; // Чуть дальше призраков
        vec2 haloUV = mirroredUV + haloVec;
        float haloWeight = pow(1.0 - distance(haloUV, vec2(0.5)) / 0.707, 15.0);
        
        if (haloWeight > 0.0) {
            flareResult += texture(bloomTexture, haloUV).rgb * haloWeight * 0.5;
        }

        // Итоговое прибавление к сцене
        finalColor += flareResult * flareIntensity;
    }
    if (enableGodRays && godRaysIntensity > 0.0) {
        vec2 deltaUV = (texCoords - lightScreenPos) * (1.0 / 30.0) * godRaysIntensity;
        vec2 rayUV = texCoords;
        float decay = 1.0;
        for(int i = 0; i < 30; i++) {
            rayUV -= deltaUV;
            if(rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0) break;
            vec3 samp = texture(screenTexture, rayUV).rgb;
            if (dot(samp, vec3(0.2126, 0.7152, 0.0722)) > 0.8) {
                finalColor += samp * decay * 0.1;
            }
            decay *= 0.95;
        }
    }

    // ==========================================
    // ЭТАП 3: ПОСТ-ОБРАБОТКА (Цвет, Тонмаппинг)
    // ==========================================

    // 3.1 Температура и Экспозиция
    finalColor *= KelvinToRGB(temperature);
    
    float exposure = currentExposure;
    if (autoExposure == 1) {
        vec3 avgColor = textureLod(screenTexture, vec2(0.5), 10.0).rgb;
        float avgLuma = max(dot(avgColor, vec3(0.2126, 0.7152, 0.0722)), 0.0001);
        exposure = clamp(0.5 / avgLuma, minBrightness, maxBrightness);
    }
    finalColor *= (exposure * exposureCompensation);

    // 3.2 Контраст и Насыщенность
    finalColor = max(vec3(0.0), (finalColor - 0.5) * contrast + 0.5);
    float luma = dot(finalColor, vec3(0.2126, 0.7152, 0.0722));
    finalColor = mix(vec3(luma), finalColor, saturation);

    // 3.3 Виньетка (исправленная)
    if (enableVignette) {
        float d = distance(texCoords, vec2(0.5));
        finalColor *= (1.0 - smoothstep(0.4, 0.8, d) * vignetteIntensity);
    }

    // 3.4 ACES Тонмаппинг
    finalColor = ACESFilm(finalColor);

    // 3.5 Sharpen и Зерно
    if (enableSharpen) {
        vec2 t = 1.0 / textureSize(screenTexture, 0);
        vec3 neighbor = (texture(screenTexture, texCoords + vec2(0, t.y)).rgb + 
                         texture(screenTexture, texCoords - vec2(0, t.y)).rgb + 
                         texture(screenTexture, texCoords + vec2(t.x, 0)).rgb + 
                         texture(screenTexture, texCoords - vec2(t.x, 0)).rgb) * 0.25;
        finalColor += (finalColor - ACESFilm(neighbor * exposure)) * sharpenIntensity;
    }

    if (enableFilmGrain) {

        float noise = random(texCoords + time) * 2.0 - 1.0; 

        finalColor += finalColor * noise * grainIntensity;

    } 

    FragColor = vec4(pow(max(finalColor, 0.0), vec3(1.0 / gamma)), 1.0);
}