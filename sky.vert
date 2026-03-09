#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main() {
    TexCoords = aPos; // Координаты куба становятся направлением взгляда!
    vec4 pos = projection * view * vec4(aPos, 1.0);
    // pos.w и pos.w дадут глубину 1.0 (самая дальняя точка) после деления перспективы
    gl_Position = pos.xyww; 
}