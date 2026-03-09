#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D screenTexture; // Наша картинка из cameraFxTexture
uniform sampler2D bloomTexture;  // Наше свечение
uniform sampler2D positionTexture; // Нужно только для объемных лучей (God Rays)
uniform float time;
uniform float gamma;
// --- Bloom & Flares ---
uniform bool enableBloom;
uniform float bloomIntensity;
uniform bool enableLensFlares;
uniform float flareIntensity;
uniform float ghostDispersal;
uniform int ghosts;
// --- God Rays ---
uniform bool enableGodRays;
uniform float godRaysIntensity;
uniform vec2 lightScreenPos;
// --- Цвет и Экспозиция ---
uniform float currentExposure;
uniform float exposureCompensation;
uniform float temperature;
uniform float contrast;
uniform float saturation;
// --- Линза и Пленка ---
uniform bool enableVignette;
uniform float vignetteIntensity;
uniform bool enableChromaticAberration;
uniform float caIntensity;
uniform bool enableFilmGrain;
uniform float grainIntensity;
uniform bool enableSharpen;
uniform float sharpenIntensity;
float random(vec2 uv) {
    return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123);
}
vec3 ACESFilm(vec3 x) {
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
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
    vec3 finalColor = vec3(0.0);
    // 1. Хроматическая аберрация (радужные края)
    if (enableChromaticAberration) {
        vec2 dir = texCoords - vec2(0.5);
        float dist = length(dir); 
        vec2 rCoords = texCoords + dir * caIntensity * dist;
        vec2 bCoords = texCoords - dir * caIntensity * dist;
        finalColor.r = texture(screenTexture, rCoords).r;
        finalColor.g = texture(screenTexture, texCoords).g;
        finalColor.b = texture(screenTexture, bCoords).b;
    } else {
        finalColor = texture(screenTexture, texCoords).rgb;
    }
    // 2. Свечение (Bloom) и блики (Lens Flares)
    if (enableBloom) {
        finalColor += texture(bloomTexture, texCoords).rgb * bloomIntensity;
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
    // 3. Объемные лучи (God Rays)
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
    // 4. Температура цвета
    vec3 tempTint = KelvinToRGB(temperature);
    finalColor *= tempTint;
    // 5. Яркость и контраст
    finalColor *= (currentExposure * exposureCompensation);
    finalColor = max(vec3(0.0), (finalColor - 0.5) * contrast + 0.5);
    float luma = dot(finalColor, vec3(0.2126, 0.7152, 0.0722));
    finalColor = mix(vec3(luma), finalColor, saturation);
    // 6. Виньетка (мягкое затемнение по краям)
    if (enableVignette) {
        float d = distance(texCoords, vec2(0.5));
        finalColor *= smoothstep(0.8, vignetteIntensity * 0.79, d * (1.0 + vignetteIntensity));
    }
    // 7. Кинематографичный цвет (ACES Tonemapping)
    finalColor = ACESFilm(finalColor);
    // 8. Резкость
    if (enableSharpen) {
        vec2 t = 1.0 / vec2(textureSize(screenTexture, 0));
        vec3 up    = ACESFilm(texture(screenTexture, texCoords + vec2(0.0, t.y)).rgb);
        vec3 down  = ACESFilm(texture(screenTexture, texCoords - vec2(0.0, t.y)).rgb);
        vec3 left  = ACESFilm(texture(screenTexture, texCoords - vec2(t.x, 0.0)).rgb);
        vec3 right = ACESFilm(texture(screenTexture, texCoords + vec2(t.x, 0.0)).rgb);
        finalColor = finalColor * (1.0 + 4.0 * sharpenIntensity) - (up + down + left + right) * sharpenIntensity;
        finalColor = max(finalColor, vec3(0.0));
    }
    // 9. Зерно пленки
    if (enableFilmGrain) {
        float noise = random(texCoords + time) * 2.0 - 1.0; 
        finalColor += finalColor * noise * grainIntensity;
    }
    // 10. Отправляем картинку на монитор!
    float safeGamma = max(gamma, 0.001);
    FragColor = vec4(pow(finalColor, vec3(1.0 / safeGamma)), 1.0);
}