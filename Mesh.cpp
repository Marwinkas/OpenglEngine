#include "Mesh.h"

Mesh::Mesh(std::vector <Vertex>& vertices, std::vector <GLuint>& indices)
{
    Mesh::vertices = vertices;
    Mesh::indices = indices;
    ID = meshid++;

    VAO.Bind();

    // Инициализируем классы, которые теперь принадлежат объекту Mesh
    VBO_obj = VBO(vertices);
    EBO_obj = EBO(indices);

    // Используем их для привязки
    VAO.LinkAttrib(VBO_obj, 0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);
    VAO.LinkAttrib(VBO_obj, 1, 3, GL_FLOAT, sizeof(Vertex), (void*)(3 * sizeof(float)));
    VAO.LinkAttrib(VBO_obj, 2, 3, GL_FLOAT, sizeof(Vertex), (void*)(6 * sizeof(float)));
    VAO.LinkAttrib(VBO_obj, 3, 2, GL_FLOAT, sizeof(Vertex), (void*)(9 * sizeof(float)));
    VAO.LinkAttrib(VBO_obj, 4, 3, GL_FLOAT, sizeof(Vertex), (void*)(11 * sizeof(float)));
    VAO.LinkAttrib(VBO_obj, 5, 3, GL_FLOAT, sizeof(Vertex), (void*)(14 * sizeof(float)));

    // Создаем инстанс VBO
    GLuint instanceVBO;
    glGenBuffers(1, &instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, 10000 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);

    std::size_t vec4Size = sizeof(glm::vec4);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(6 + i);
        glVertexAttribPointer(6 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
        glVertexAttribDivisor(6 + i, 1);
    }

    // Снова биндим основной VBO для костей
    VBO_obj.Bind();
    glEnableVertexAttribArray(10);
    glVertexAttribIPointer(10, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    VAO.Unbind();
    VBO_obj.Unbind();
    EBO_obj.Unbind();

    // --- САМОЕ ВАЖНОЕ ИСПРАВЛЕНИЕ ---
    VBOS.ID = instanceVBO;
    VBOS.target = GL_ARRAY_BUFFER; // Без этого SetData не работал!
}