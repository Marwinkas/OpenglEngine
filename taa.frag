#version 330 core
out vec4 FragColor;
in vec2 texCoords;

uniform sampler2D currentColor;
uniform sampler2D historyColor;
// ИСПРАВЛЕНИЕ: Заменили позицию на глубину
uniform sampler2D depthTexture; 

uniform mat4 currentCleanViewProj; 
uniform mat4 prevViewProj;
uniform mat4 invViewProj; // Добавили для восстановления позиции

// Функция восстановления позиции
vec3 ReconstructWorldPos(vec2 uv) {
    float depth = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invViewProj * ndc;
    return worldPos.xyz / worldPos.w;
}

void main() {
    vec3 currentRGB = texture(currentColor, texCoords).rgb;
    float depth = texture(depthTexture, texCoords).r;
    
    // Если это небо (глубина максимальная) - не делаем TAA
    if(depth >= 0.9999) {
        FragColor = vec4(currentRGB, 1.0);
        return;
    }

    // Восстанавливаем позицию пикселя в мире
    vec3 pos = ReconstructWorldPos(texCoords);

    vec4 currClip = currentCleanViewProj * vec4(pos, 1.0);
    vec2 currNDC = currClip.xy / currClip.w;
    vec2 currUV = currNDC * 0.5 + 0.5;
    
    vec4 prevClip = prevViewProj * vec4(pos, 1.0);
    vec2 prevNDC = prevClip.xy / prevClip.w;
    vec2 prevUV = prevNDC * 0.5 + 0.5;
    
    vec2 velocity = currUV - prevUV;
    vec2 historyTexCoord = texCoords - velocity;
    
    if(historyTexCoord.x < 0.0 || historyTexCoord.x > 1.0 || historyTexCoord.y < 0.0 || historyTexCoord.y > 1.0) {
        FragColor = vec4(currentRGB, 1.0);
        return;
    }
    
    vec3 historyRGB = texture(historyColor, historyTexCoord).rgb;
    vec2 texelSize = 1.0 / textureSize(currentColor, 0);
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec3 c = texture(currentColor, texCoords + vec2(x, y) * texelSize).rgb;
            m1 += c;
            m2 += c * c;
        }
    }
    
    vec3 mu = m1 / 9.0;
    vec3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));
    float gamma = 1.5; 
    vec3 minColor = mu - gamma * sigma;
    vec3 maxColor = mu + gamma * sigma;
    
    historyRGB = clamp(historyRGB, minColor, maxColor);
    FragColor = vec4(mix(historyRGB, currentRGB, 0.05), 1.0);
}