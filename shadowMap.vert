#version 330 core
layout (location = 0) in vec3 aPos;
// Используем 6-ю локацию, как в рабочем кубмапе!
layout (location = 6) in mat4 instanceModel; 
uniform mat4 lightProjection;
void main()
{
    // Матрица солнца (Projection * View) * Матрица объекта из инстанса * Координаты
    gl_Position = lightProjection * instanceModel * vec4(aPos, 1.0);
}