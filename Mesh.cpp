#include "Mesh.h"

Mesh::Mesh(std::vector <Vertex>& vertices, std::vector <GLuint>& indices)
{
	Mesh::vertices = vertices;
	Mesh::indices = indices;
	ID = meshid++;
	
	VAO.Bind();
	// Generates Vertex Buffer Object and links it to vertices
	VBO VBO(vertices);
	// Generates Element Buffer Object and links it to indices
	EBO EBO(indices);
	// Links VBO attributes such as coordinates and colors to VAO
	VAO.LinkAttrib(VBO, 0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);
	VAO.LinkAttrib(VBO, 1, 3, GL_FLOAT, sizeof(Vertex), (void*)(3 * sizeof(float)));
	VAO.LinkAttrib(VBO, 2, 3, GL_FLOAT, sizeof(Vertex), (void*)(6 * sizeof(float)));
	VAO.LinkAttrib(VBO, 3, 2, GL_FLOAT, sizeof(Vertex), (void*)(9 * sizeof(float)));
	VAO.LinkAttrib(VBO, 4, 3, GL_FLOAT, sizeof(Vertex), (void*)(11 * sizeof(float)));
	VAO.LinkAttrib(VBO, 5, 3, GL_FLOAT, sizeof(Vertex), (void*)(14 * sizeof(float))); // bitangentSign


	GLuint instanceVBO;
	glGenBuffers(1, &instanceVBO);
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
	glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);

	std::size_t vec4Size = sizeof(glm::vec4);
	for (int i = 0; i < 4; i++) {
		glEnableVertexAttribArray(6 + i);
		glVertexAttribPointer(6 + i, 4, GL_FLOAT, GL_FALSE,
			sizeof(glm::mat4), (void*)(i * vec4Size));
		glVertexAttribDivisor(6 + i, 1);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	// Unbind all to prevent accidentally modifying them
	VAO.Unbind();
	VBO.Unbind();
	EBO.Unbind();
	VBOS = instanceVBO;
}


void Mesh::Draw
(
	Shader& shader, 
	Camera& camera,
	Transform& transform,
	Material& material
)
{
	// Bind shader to be able to access uniforms
	shader.Activate();
	VAO.Bind();
	material.Activate(shader);
	// Take care of the camera Matrix
	glUniform3f(glGetUniformLocation(shader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
	camera.Matrix(shader, "camMatrix");
	glm::mat4 matrix = glm::mat4(1.0f);
	ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(transform.position), glm::value_ptr(transform.rotation), glm::value_ptr(transform.scale),
		glm::value_ptr(matrix));

	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(matrix));

	// Draw the actual mesh
	glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
}

void Mesh::DrawShadow(
	Shader& shader,
	Transform& transform
)
{
	// Активируем шейдер для теней
	shader.Activate();
	VAO.Bind();

	// Initialize matrices
	glm::mat4 model = glm::mat4(1.0f);
	glm::mat4 matrix = glm::mat4(1.0f);
	ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(transform.position), glm::value_ptr(transform.rotation), glm::value_ptr(transform.scale),
		glm::value_ptr(matrix));
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(matrix));

	// Рисуем меш (только геометрию, без текстур)
	glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
}