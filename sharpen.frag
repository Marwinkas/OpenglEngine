#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D screenTexture;
uniform float sharpenAmount; 
void main() {
    vec3 center = texture(screenTexture, texCoords).rgb;
        if (sharpenAmount <= 0.0) {
        FragColor = vec4(center, 1.0);
        return;
    }
        vec2 texelSize = 1.0 / textureSize(screenTexture, 0);
        vec3 up    = texture(screenTexture, texCoords + vec2(0.0, texelSize.y)).rgb;
    vec3 down  = texture(screenTexture, texCoords - vec2(0.0, texelSize.y)).rgb;
    vec3 left  = texture(screenTexture, texCoords + vec2(texelSize.x, 0.0)).rgb;
    vec3 right = texture(screenTexture, texCoords - vec2(texelSize.x, 0.0)).rgb;
        vec3 edges = (up + down + left + right) * 0.25;
        vec3 sharpened = center + (center - edges) * sharpenAmount;
        FragColor = vec4(max(sharpened, 0.0), 1.0);
}