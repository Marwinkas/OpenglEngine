#version 330 core
out vec3 FragColor;
in vec2 texCoords;

uniform sampler2D positionTexture;
uniform sampler2D normalTexture;
uniform sampler2D texNoise;
uniform vec3 samples[64]; // Массив оставляем 64, чтобы не менять C++ код
uniform mat4 projection;
uniform mat4 view;
uniform vec2 noiseScale;
uniform bool enableSSAO;
uniform float radius;
uniform float bias;
uniform float intensity;
uniform float power;
void main() {
    if (!enableSSAO) { FragColor = vec3(1.0); return; }
    
    vec3 worldPos = texture(positionTexture, texCoords).rgb;
    if (length(worldPos) < 0.1) { FragColor = vec3(1.0); return; }

    vec3 normalOrig = texture(normalTexture, texCoords).rgb;
    vec3 fragPos = (view * vec4(worldPos, 1.0)).xyz;
    vec3 normal = normalize(mat3(view) * normalOrig);
    
    // МАГИЯ 1: Отодвигаем старт лучей от поверхности на 5-10 см.
    // Это убирает ложную грязь на плоских стенах и спинах!
    fragPos += normal * 0.1; 

    vec3 randomVec = normalize(texture(texNoise, texCoords * noiseScale).xyz);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    float occlusion = 0.0;
    float validSamples = 0.0; // Считаем только полезные лучи!
    
    int sampleCount = 24; 
    
    for(int i = 0; i < sampleCount; ++i) {
        vec3 samplePos = TBN * samples[i]; 
        samplePos = fragPos + samplePos * radius; 
        
        vec4 offset = vec4(samplePos, 1.0);
        offset = projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        
        vec3 sampleWorldPos = texture(positionTexture, offset.xy).rgb;
        if (length(sampleWorldPos) < 0.1) continue; // Улетел в небо - в мусорку
        
        float sampleDepth = (view * vec4(sampleWorldPos, 1.0)).z;
        float zDiff = abs(fragPos.z - sampleDepth);

        // МАГИЯ 2: УБИЙЦА ОРЕОЛОВ
        // Если луч ударился о дальнюю стену за спиной персонажа (zDiff > radius),
        // мы его не засчитываем! Он не будет осветлять наш край.
        if (zDiff > radius) {
            continue; 
        }
        
        validSamples += 1.0; 
        
        float rangeCheck = smoothstep(0.0, 1.0, radius / zDiff);
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;      
    }

    // Делим только на те лучи, которые попали в наше тело!
    float shadowFactor = (validSamples > 0.0) ? (occlusion / validSamples) : 0.0;
    
    shadowFactor *= intensity;
    float res = 1.0 - shadowFactor;
    res = pow(max(res, 0.0), power);
    res = clamp(res, 0.2, 1.0);
    FragColor = vec3(res);
}