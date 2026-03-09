#version 330 core
out vec4 FragColor;
in vec2 texCoords;

uniform sampler2D image; // То, что размываем
uniform sampler2D positionTexture;
uniform sampler2D normalTexture;

uniform bool horizontal;

// Настройки размытия (потом можно вынести в UI)
const int blurRadius = 4; // Линейный радиус 4 = 9 выборок (вместо 81!)
const float posThreshold = 0.5; // Насколько сильно объекты должны быть рядом
const float normThreshold = 0.8; // Насколько нормали должны совпадать

void main() {
    vec2 texelSize = 1.0 / textureSize(image, 0);
    
    // Информация о центральном пикселе
    vec3 centerColor = texture(image, texCoords).rgb;
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    vec3 centerNorm = texture(normalTexture, texCoords).rgb;
    
    // Небо не размываем
    if(length(centerPos) < 0.01) {
        FragColor = vec4(centerColor, 1.0);
        return;
    }

    vec3 result = centerColor;
    float totalWeight = 1.0;
    
    for(int i = -blurRadius; i <= blurRadius; ++i) {
        if(i == 0) continue; // Центр мы уже добавили
        
        vec2 offset = horizontal ? vec2(float(i), 0.0) : vec2(0.0, float(i));
        vec2 sampleUV = texCoords + offset * texelSize;
        
        vec3 samplePos = texture(positionTexture, sampleUV).rgb;
        vec3 sampleNorm = texture(normalTexture, sampleUV).rgb;
        
        // Магия Bilateral: если точка слишком далеко или смотрит в другую сторону - вес падает
        float posDiff = length(centerPos - samplePos);
        float normDiff = max(0.0, dot(centerNorm, sampleNorm));
        
        // Формируем вес (чем ближе и похоже, тем ближе вес к 1.0)
        float weight = exp(-posDiff * posThreshold) * pow(normDiff, normThreshold);
        
        // Дополнительный вес по дистанции на экране (Гаусс)
        float spatialWeight = 1.0 - (abs(float(i)) / float(blurRadius));
        weight *= spatialWeight;
        
        result += texture(image, sampleUV).rgb * weight;
        totalWeight += weight;
    }
    
    FragColor = vec4(result / totalWeight, 1.0);
}