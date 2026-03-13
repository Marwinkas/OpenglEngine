#version 460 core
#extension GL_ARB_shader_draw_parameters : require // Позволяет использовать gl_DrawID
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
out vec3 crntPos;
out vec3 Normal;
out vec3 Tangent;
out vec2 texCoord;
out vec4 fragPosLight;
out mat3 TBN;
out vec3 TangentViewPos;
out vec3 TangentFragPos;
uniform mat4 camMatrix;
uniform mat4 lightProjection;
uniform vec3 camPos;
struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint pad1, pad2, pad3;
};

layout(std430, binding = 10) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

// Отправляем ID материала во фрагментный шейдер
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
    vec3 T = normalize(normalMatrix * aTangent);
    vec3 N = normalize(normalMatrix * aNormal);
    vec3 B = normalize(normalMatrix * aBitangent); 
    TBN = mat3(T, B, N);
    TangentViewPos = crntPos; 
    TangentFragPos = camPos - crntPos;
    Normal = N;
        texCoord = aTex;
    fragPosLight = lightProjection * vec4(crntPos, 1.0);
        gl_Position = camMatrix * worldPos; 
}