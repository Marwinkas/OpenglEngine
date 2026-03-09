#version 330 core
out vec3 FragColor;
in vec2 texCoords;

uniform sampler2D positionTexture;
uniform sampler2D normalTexture;
uniform sampler2D texNoise;

uniform vec3 samples[64];
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
    
    vec3 normalOrig = texture(normalTexture, texCoords).rgb;
    // Бронебойная проверка на небо: если нормали нет, значит это пустота!
    if (length(normalOrig) < 0.1) { FragColor = vec3(1.0); return; }
    
    vec3 worldPos = texture(positionTexture, texCoords).rgb;

    // Переводим в пространство вида (View Space)
    vec3 fragPos = (view * vec4(worldPos, 1.0)).xyz;
    vec3 normal = normalize(mat3(view) * normalOrig);
    vec3 randomVec = normalize(texture(texNoise, texCoords * noiseScale).xyz);
    // Создаем базис TBN для вращения выборки
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;

    for(int i = 0; i < 64; ++i) {
        vec3 samplePos = TBN * samples[i]; 
        samplePos = fragPos + samplePos * radius; 
        
        vec4 offset = vec4(samplePos, 1.0);
        offset = projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        
        vec3 sampleWorldPos = texture(positionTexture, offset.xy).rgb;
        
        // --- НОВАЯ ЗАЩИТА ---
        // Узнаем нормаль того пикселя, куда попал сэмпл
        vec3 sampleNorm = texture(normalTexture, offset.xy).rgb;
        // Если нормали нет (это наше небо), то этот сэмпл игнорируем!
        if (length(sampleNorm) < 0.01) {
            continue; 
        }
        // --------------------

        float sampleDepth = (view * vec4(sampleWorldPos, 1.0)).z;

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;      
    }
    
// 1. Получаем среднее значение затенения (от 0 до 1)
    float shadowFactor = occlusion / 64.0;

    // 2. Усиливаем интенсивность тени
    shadowFactor *= intensity;

    // 3. Вычитаем тень из чистого белого света
    float res = 1.0 - shadowFactor;

    // 4. Делаем тени более контрастными (power)
    // Чем выше степень, тем резче будут тени в глубоких углах
    res = pow(max(res, 0.0), power);

    // 5. Защита "от дурака" (чтобы экран никогда не был в 0)
    // Даже в самой глубокой тени останется 20% света
    res = clamp(res, 0.2, 1.0);

    FragColor = vec3(res);
}