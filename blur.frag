#version 330 core
out vec4 FragColor;
in vec2 texCoords;

uniform sampler2D screenTexture; 
uniform sampler2D ssgiTexture;   
uniform sampler2D normalTexture;
uniform sampler2D positionTexture;
uniform sampler2D ssaoTexture;
uniform sampler2D bloomTexture;

uniform float gamma;
uniform int blurRange;

// Фильтры
uniform float contrast;
uniform float saturation;
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

// Exposure & Temp
uniform float currentExposure;
uniform float exposureCompensation;
uniform float temperature;

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

uniform bool enableFilmGrain;
uniform float grainIntensity;

uniform bool enableSharpen;
uniform float sharpenIntensity;
// --- FOG ---
uniform bool enableFog;
uniform float fogDensity;
uniform float fogHeightFalloff;
uniform float fogBaseHeight;
uniform vec3 fogColor;
uniform vec3 inscatterColor;
uniform float inscatterPower;
uniform float inscatterIntensity;
uniform vec3 sunDirFog;
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

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssgiTexture, 0));
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    vec3 centerNormal = texture(normalTexture, texCoords).rgb;
    
    vec3 baseColor = vec3(0.0);
    bool isSky = length(centerPos) < 0.01;

    // ==========================================
    // ЭТАП 1: ЛОКАЛЬНЫЕ ЭФФЕКТЫ (Геометрия vs Небо)
    // ==========================================
    if (isSky) {
        // ЕСЛИ ЭТО НЕБО: Берем чистый цвет и не считаем тени/DoF
        baseColor = texture(screenTexture, texCoords).rgb;
    } 
    else {
        // ЕСЛИ ЭТО ГЕОМЕТРИЯ: Считаем полный фарш
        // 1. Хроматическая аберрация
        vec3 directLight = vec3(0.0);
        if (enableChromaticAberration) {
            vec2 dir = texCoords - vec2(0.5);
            float dist = length(dir); 
            vec2 rCoords = texCoords + dir * caIntensity * dist;
            vec2 bCoords = texCoords - dir * caIntensity * dist;
            directLight.r = texture(screenTexture, rCoords).r;
            directLight.g = texture(screenTexture, texCoords).g;
            directLight.b = texture(screenTexture, bCoords).b;
        } else {
            directLight = texture(screenTexture, texCoords).rgb;
        }

        // 2. SSGI (Отражения)
        vec3 blurSSGI = vec3(0.0);
        float totalWeight = 0.0;
        for(int x = -blurRange; x <= blurRange; ++x) {
            for(int y = -blurRange; y <= blurRange; ++y) {
                vec2 offset = vec2(float(x), float(y)) * texelSize;
                vec3 sPos = texture(positionTexture, texCoords + offset).rgb;
                vec3 sNorm = texture(normalTexture, texCoords + offset).rgb;
                vec3 sSSGI = texture(ssgiTexture, texCoords + offset).rgb;

                float w = pow(max(0.0, dot(centerNormal, sNorm)), 2.0); 
                float distWeight = exp(-length(centerPos - sPos) * 1.0); 
                float finalWeight = w * distWeight + 0.0001; 

                blurSSGI += sSSGI * finalWeight;
                totalWeight += finalWeight;
            }
        }
        vec3 indirect = blurSSGI / totalWeight;

        // 3. SSAO (Микротени)
        float ambientOcclusion = texture(ssaoTexture, texCoords).r;
        baseColor = (directLight + indirect * 2.0) * ambientOcclusion;
        
        if (enableFog) {
            // Дистанция от камеры до пикселя
            float distToCamFog = length(centerPos - camPos);
            
            // Насколько высоко находится этот пиксель над базой тумана
            float height = centerPos.y - fogBaseHeight;
            
            // Считаем экспоненциальное падение плотности с высотой
            float heightDensity = exp(-height * fogHeightFalloff);
            
            // Итоговая сила тумана (от 0.0 до 1.0)
            float fogFactor = 1.0 - exp(-distToCamFog * fogDensity * heightDensity);
            fogFactor = clamp(fogFactor, 0.0, 1.0);
            
            // Объемное свечение (Inscattering) - если смотрим в сторону солнца
            vec3 viewDirFog = normalize(centerPos - camPos);
            vec3 L = normalize(-sunDirFog); 
            float inscatterFactor = pow(max(dot(viewDirFog, L), 0.0), inscatterPower);
            
            // Смешиваем обычный цвет тумана с цветом солнца
            vec3 currentFogColor = fogColor + (inscatterColor * inscatterFactor * inscatterIntensity);
            
            // Накладываем туман на нашу картинку!
            baseColor = mix(baseColor, currentFogColor, fogFactor);
        }

        // 4. Depth of Field (Размытие фона)
        float distToCam = length(centerPos - camPos); 
        float dofFactor = 0.0;
        if (enableDoF) {
            dofFactor = abs(distToCam - focusDistance) - focusRange;
            dofFactor = clamp(dofFactor / max(bokehSize, 0.001), 0.0, 1.0); 
        }
        if (enableDoF && dofFactor > 0.0) {
            baseColor = mix(baseColor, indirect * 1.5, dofFactor); 
        }

        // 5. Motion Blur (Размытие в движении только для объектов)
// 5. Motion Blur (Улучшенный, с проверкой глубины и теней)
        if (enableMotionBlur) {
            vec4 prevClip = prevViewProj * vec4(centerPos, 1.0);
            vec2 prevNDC = prevClip.xy / prevClip.w;
            vec2 prevUV = prevNDC * 0.5 + 0.5;
            
            // Вектор скорости пикселя
            vec2 velocity = (texCoords - prevUV) * mbStrength;
            
            int mbSamples = 6;
            vec3 mbColor = baseColor; // Начинаем с текущего (уже затененного) пикселя
            float totalWeightMB = 1.0;
            
            for(int i = 1; i < mbSamples; ++i) {
                // Вычисляем координаты соседа по линии движения
                vec2 offsetUV = texCoords + velocity * (float(i) / float(mbSamples - 1) - 0.5);
                
                // Читаем позицию соседа, чтобы узнать, где он в 3D
                vec3 sPos = texture(positionTexture, offsetUV).rgb;
                
                // ЗАЩИТА ОТ МЫЛА:
                // Если сосед это небо (<0.01) или разница в глубине между нами больше 1.5 метров
                // значит это край объекта. Мы не смешиваем их, чтобы сохранить резкость!
                if (length(sPos) < 0.01 || abs(length(sPos) - length(centerPos)) > 1.5) {
                    continue; // Пропускаем этого соседа
                }

                // Читаем цвет соседа
                vec3 sColor = texture(screenTexture, offsetUV).rgb;
                // Читаем тень (SSAO) соседа!
                float sSSAO = texture(ssaoTexture, offsetUV).r;
                
                // Добавляем цвет соседа ВМЕСТЕ с его тенью
                mbColor += sColor * sSSAO;
                totalWeightMB += 1.0; // Считаем, сколько реально пикселей подошло
            }
            
            // Усредняем только по тем пикселям, которые прошли проверку
            baseColor = mbColor / totalWeightMB;
        }
    }

    vec3 finalColor = baseColor;

    // ==========================================
    // ЭТАП 2: ГЛОБАЛЬНЫЕ ЭФФЕКТЫ (Накладываются поверх ВСЕГО)
    // ==========================================

    // 1. BLOOM И FLARES (Вернулись к жизни!)
    if (enableBloom) {
        vec3 bloomColor = texture(bloomTexture, texCoords).rgb;
        finalColor += bloomColor * bloomIntensity;
    }

    if (enableLensFlares) {
        vec2 texcoord = -texCoords + vec2(1.0); 
        vec2 ghostVec = (vec2(0.5) - texcoord) * ghostDispersal;
        vec3 flareResult = vec3(0.0);
        for (int i = 0; i < ghosts; ++i) {
            vec2 offset = fract(texcoord + ghostVec * float(i));
            float weight = pow(1.0 - length(vec2(0.5) - offset) / length(vec2(0.5)), 10.0); 
            flareResult += texture(bloomTexture, offset).rgb * weight;
        }
        finalColor += flareResult * flareIntensity;
    }

    // 2. GOD RAYS (Объемные лучи)
    if (enableGodRays && godRaysIntensity > 0.0) {
        vec2 deltaUV = (texCoords - lightScreenPos);
        deltaUV *= 1.0 / 30.0 * godRaysIntensity; 
        
        vec2 rayUV = texCoords;
        float illuminationDecay = 1.0;
        vec3 godRaysColor = vec3(0.0);
        
        for(int i = 0; i < 30; i++) {
            rayUV -= deltaUV;
            vec3 samp = texture(bloomTexture, rayUV).rgb;
            samp *= illuminationDecay * 0.1; 
            godRaysColor += samp;
            illuminationDecay *= 0.95; 
        }
        finalColor += godRaysColor;
    }

    // 3. ТЕМПЕРАТУРА
    vec3 tempTint = KelvinToRGB(temperature);
    finalColor *= tempTint;

    // 4. ЭКСПОЗИЦИЯ И КОЛОРИНГ
    finalColor *= (currentExposure * exposureCompensation);
    finalColor = max(vec3(0.0), (finalColor - 0.5) * contrast + 0.5);

    float luma = dot(finalColor, vec3(0.2126, 0.7152, 0.0722));
    finalColor = mix(vec3(luma), finalColor, saturation);

    if (enableVignette) {
        float d = distance(texCoords, vec2(0.5));
        finalColor *= smoothstep(0.8, vignetteIntensity * 0.79, d * (1.0 + vignetteIntensity));
    }
    
    // 5. ТОНМАППИНГ (ACES)
    finalColor = ACESFilm(finalColor);

    // 6. SHARPEN И ЗЕРНО
    if (enableSharpen) {
        vec2 t = 1.0 / vec2(textureSize(screenTexture, 0))
        vec3 up    = ACESFilm(texture(screenTexture, texCoords + vec2(0.0, t.y)).rgb);
        vec3 down  = ACESFilm(texture(screenTexture, texCoords - vec2(0.0, t.y)).rgb);
        vec3 left  = ACESFilm(texture(screenTexture, texCoords - vec2(t.x, 0.0)).rgb);
        vec3 right = ACESFilm(texture(screenTexture, texCoords + vec2(t.x, 0.0)).rgb);
        
        finalColor = finalColor * (1.0 + 4.0 * sharpenIntensity) - (up + down + left + right) * sharpenIntensity;
        finalColor = max(finalColor, vec3(0.0));
    }

    if (enableFilmGrain) {
        float noise = random(texCoords + time) * 2.0 - 1.0; 
        finalColor += finalColor * noise * grainIntensity;
    }

    float safeGamma = max(gamma, 0.001);
    FragColor = vec4(pow(finalColor, vec3(1.0 / safeGamma)), 1.0);
}