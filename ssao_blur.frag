#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D ssaoInput;
uniform sampler2D positionTexture; 
void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;
    float totalWeight = 0.0;
    vec3 centerPos = texture(positionTexture, texCoords).rgb;
    if (length(centerPos) < 0.1) {
        FragColor = vec4(1.0);
        return;
    }
        for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUV = texCoords + offset;
            float sampleSSAO = texture(ssaoInput, sampleUV).r;
            vec3 samplePos = texture(positionTexture, sampleUV).rgb;
                                    float weight = (distance(centerPos, samplePos) < 0.3) ? 1.0 : 0.0;
            result += sampleSSAO * weight;
            totalWeight += weight;
        }
    }
    FragColor = vec4(result / max(totalWeight, 0.0001));
}