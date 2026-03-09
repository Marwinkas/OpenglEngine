#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D screenTexture; // Оригинальная картинка без эффектов
uniform sampler2D positionTexture;
uniform sampler2D normalTexture;
uniform sampler2D ssaoTexture;
uniform sampler2D ssgiTexture;
uniform vec3 camPos;
uniform int blurRange;
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
void main() {
    // Безопасное вычисление размера пикселя
    vec2 texelSize = 1.0 / vec2(textureSize(ssgiTexture, 0));
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    vec3 centerNormal = texture(normalTexture, texCoords).rgb;
    vec3 baseColor = vec3(0.0);
    bool isSky = length(centerPos) < 0.01;
    // ==========================================
    // ЛОКАЛЬНЫЕ ЭФФЕКТЫ (Геометрия vs Небо)
    // ==========================================
    if (isSky) {
        // Небо оставляем чистым
        baseColor = texture(screenTexture, texCoords).rgb;
    } 
    else {
        vec3 directLight = texture(screenTexture, texCoords).rgb;
        // 1. SSGI (Отражения и размытие)
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
        // 2. SSAO (Микротени)
        float ambientOcclusion = texture(ssaoTexture, texCoords).r;
        baseColor = (directLight + indirect * 2.0) * ambientOcclusion;
        // 3. FOG (Атмосферный туман)
        if (enableFog) {
            float distToCamFog = length(centerPos - camPos);
            float height = centerPos.y - fogBaseHeight;
            float heightDensity = exp(-height * fogHeightFalloff);
            float fogFactor = 1.0 - exp(-distToCamFog * fogDensity * heightDensity);
            fogFactor = clamp(fogFactor, 0.0, 1.0);
            vec3 viewDirFog = normalize(centerPos - camPos);
            vec3 L = normalize(-sunDirFog); 
            float inscatterFactor = pow(max(dot(viewDirFog, L), 0.0), inscatterPower);
            vec3 currentFogColor = fogColor + (inscatterColor * inscatterFactor * inscatterIntensity);
            baseColor = mix(baseColor, currentFogColor, fogFactor);
        }
    }
    // Отправляем готовую базу (в HDR) на следующий этап!
    FragColor = vec4(baseColor, 1.0);
}