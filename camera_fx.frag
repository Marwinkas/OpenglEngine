#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D combineTexture;  // Картинка с тенями и туманом
uniform sampler2D positionTexture; // Нужно для определения глубины
uniform vec3 camPos;
// --- Depth of Field ---
uniform bool enableDoF;
uniform float focusDistance;
uniform float focusRange;
uniform float bokehSize;
// --- Motion Blur ---
uniform bool enableMotionBlur;
uniform float mbStrength;
uniform mat4 prevViewProj;
void main() {
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    vec3 baseColor = texture(combineTexture, texCoords).rgb;
    // Если это небо (глубина ~0), не применяем DoF и Motion Blur
    if (length(centerPos) < 0.01) {
        FragColor = vec4(baseColor, 1.0);
        return;
    }
    // ==========================================
    // 1. Depth of Field (Размытие фона)
    // ==========================================
    float distToCam = length(centerPos - camPos); 
    float dofFactor = 0.0;
    if (enableDoF) {
        dofFactor = abs(distToCam - focusDistance) - focusRange;
        dofFactor = clamp(dofFactor / max(bokehSize, 0.001), 0.0, 1.0); 
    }
    vec3 dofColor = baseColor;
    if (enableDoF && dofFactor > 0.0) {
        // Простой и быстрый Box Blur для фона
        vec2 texelSize = 1.0 / vec2(textureSize(combineTexture, 0));
        vec3 blurColor = vec3(0.0);
        float weight = 0.0;
        for(int x = -1; x <= 1; ++x) {
            for(int y = -1; y <= 1; ++y) {
                blurColor += texture(combineTexture, texCoords + vec2(x, y) * texelSize * 2.0).rgb;
                weight += 1.0;
            }
        }
        dofColor = mix(baseColor, blurColor / weight, dofFactor);
    }
    // ==========================================
    // 2. Motion Blur (Размытие в движении)
    // ==========================================
    vec3 finalColor = dofColor;
    if (enableMotionBlur) {
        vec4 prevClip = prevViewProj * vec4(centerPos, 1.0);
        vec2 prevNDC = prevClip.xy / prevClip.w;
        vec2 prevUV = prevNDC * 0.5 + 0.5;
        vec2 velocity = (texCoords - prevUV) * mbStrength;
        int mbSamples = 6;
        vec3 tempColor = dofColor;
        float totalWeightMB = 1.0;
        for(int i = 1; i < mbSamples; ++i) {
            vec2 offsetUV = texCoords + velocity * (float(i) / float(mbSamples - 1) - 0.5);
            vec3 sPos = texture(positionTexture, offsetUV).rgb;
            // Защита от смазывания с фоном/небом
            if (length(sPos) < 0.01 || abs(length(sPos) - length(centerPos)) > 1.5) {
                continue; 
            }
            tempColor += texture(combineTexture, offsetUV).rgb;
            totalWeightMB += 1.0; 
        }
        finalColor = tempColor / totalWeightMB;
    }
    FragColor = vec4(finalColor, 1.0);
}