#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 6) in mat4 instanceModel; 

// Просто принимаем ту матрицу, которую заботливо прислал C++
uniform mat4 lightProjection; 

void main()
{
    gl_Position = lightProjection * instanceModel * vec4(aPos, 1.0);
}