#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D image; uniform sampler2D positionTexture;
uniform sampler2D normalTexture;
uniform bool horizontal;
const int blurRadius = 4; const float posThreshold = 0.5; const float normThreshold = 0.8; 
void main() {
    vec2 texelSize = 1.0 / textureSize(image, 0);
        vec3 centerColor = texture(image, texCoords).rgb;
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    vec3 centerNorm = texture(normalTexture, texCoords).rgb;
        if(length(centerPos) < 0.01) {
        FragColor = vec4(centerColor, 1.0);
        return;
    }
    vec3 result = centerColor;
    float totalWeight = 1.0;
    for(int i = -blurRadius; i <= blurRadius; ++i) {
        if(i == 0) continue;         
        vec2 offset = horizontal ? vec2(float(i), 0.0) : vec2(0.0, float(i));
        vec2 sampleUV = texCoords + offset * texelSize;
        vec3 samplePos = texture(positionTexture, sampleUV).rgb;
        vec3 sampleNorm = texture(normalTexture, sampleUV).rgb;
                float posDiff = length(centerPos - samplePos);
        float normDiff = max(0.0, dot(centerNorm, sampleNorm));
                float weight = exp(-posDiff * posThreshold) * pow(normDiff, normThreshold);
                float spatialWeight = 1.0 - (abs(float(i)) / float(blurRadius));
        weight *= spatialWeight;
        result += texture(image, sampleUV).rgb * weight;
        totalWeight += weight;
    }
    FragColor = vec4(result / totalWeight, 1.0);
}