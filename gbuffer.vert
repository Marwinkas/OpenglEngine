#version 460 core
#extension GL_ARB_shader_draw_parameters : require
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out vec3 crntPos;
out vec3 Normal;
out vec2 texCoord;
out mat3 TBN; // Нам нужен TBN для нормалмапов

uniform mat4 camMatrix;

struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint pad1, pad2, pad3;
};

layout(std430, binding = 10) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

flat out uint matID; 

void main()
{
    uint globalIndex = gl_BaseInstanceARB + gl_InstanceID; 
    ObjectData obj = objects[globalIndex]; 
    mat4 instanceMatrix = obj.modelMatrix;
    matID = obj.materialID;

    vec4 worldPos = instanceMatrix * vec4(aPos, 1.0f);
    crntPos = worldPos.xyz;
    
    mat3 normalMatrix = transpose(inverse(mat3(instanceMatrix)));
    
    // БЕЗОПАСНЫЙ РАСЧЕТ TBN
    vec3 N = normalize(normalMatrix * aNormal);
    Normal = N;
    texCoord = aTex;

    // Проверяем, есть ли вообще тангенсы у модели (длина вектора > 0)
    if (length(aTangent) > 0.0) {
        vec3 T = normalize(normalMatrix * aTangent);
        // Ортогонализация (Грам-Шмидт) для идеальной точности
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T); 
        TBN = mat3(T, B, N);
    } else {
        // Если тангенсов нет, ставим единичную матрицу, чтобы шейдер не взорвался от NaN
        TBN = mat3(1.0); 
    }
    
    gl_Position = camMatrix * worldPos; 
}