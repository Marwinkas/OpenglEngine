#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in vec2 aTex;
layout (location = 4) in vec3 aTangent;
layout (location = 5) in vec3 aBitangent;
layout (location = 6) in mat4 instanceModel;
out vec3 crntPos;
out vec3 Normal;
out vec3 Tangent;
out vec3 color;
out vec2 texCoord;
out vec4 fragPosLight;
out mat3 TBN;
out vec3 TangentViewPos;
out vec3 TangentFragPos;
uniform mat4 camMatrix;
uniform mat4 model;
uniform mat4 lightProjection;
uniform vec3 camPos;
void main()
{
	vec4 worldPos = instanceModel * vec4(aPos, 1.0);
	crntPos = worldPos.xyz;

	// правильная normalMatrix без влияния масштаба
	mat3 normalMatrix =  transpose(inverse (mat3(instanceModel)));


	vec3 T = normalize(normalMatrix * aTangent);
	vec3 B = normalize(normalMatrix * aBitangent);
	vec3 N = normalize(normalMatrix * aNormal);

	TBN = mat3(T, B, N);
	TangentViewPos = crntPos; // view vector в tangent space
	TangentFragPos = camPos - crntPos;
	Normal = N;

	texCoord = aTex;
	color = aColor;
	fragPosLight = lightProjection * vec4(crntPos, 1.0);
	gl_Position = camMatrix * instanceModel * vec4(aPos, 1.0);
}
