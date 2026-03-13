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
// 0: Позиция - 3 компонента (float)
    VAO.LinkAttrib(VBO_obj, 0, 3, GL_FLOAT, sizeof(Vertex), (void*)offsetof(Vertex, position), false);

    // 1: Нормаль - СТРОГО 4 компонента (требование 10_10_10_2), нормализация = true!
    VAO.LinkAttrib(VBO_obj, 1, 4, GL_INT_2_10_10_10_REV, sizeof(Vertex), (void*)offsetof(Vertex, normal), true);

    // 2: UV координаты - 2 компонента (half-float)
    VAO.LinkAttrib(VBO_obj, 2, 2, GL_HALF_FLOAT, sizeof(Vertex), (void*)offsetof(Vertex, texUV), false);

    // 3: Тангенс - СТРОГО 4 компонента, нормализация = true!
    VAO.LinkAttrib(VBO_obj, 3, 4, GL_INT_2_10_10_10_REV, sizeof(Vertex), (void*)offsetof(Vertex, tangent), true);

    // 4: Битангенс - СТРОГО 4 компонента, нормализация = true!
    VAO.LinkAttrib(VBO_obj, 4, 4, GL_INT_2_10_10_10_REV, sizeof(Vertex), (void*)offsetof(Vertex, bitangent), true);

    // Создаем инстанс VBO
    GLuint instanceVBO;
    glGenBuffers(1, &instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, 10000 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);

    std::size_t vec4Size = sizeof(glm::vec4);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(5 + i);
        glVertexAttribPointer(5 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
        glVertexAttribDivisor(5 + i, 1);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    VAO.Unbind();
    VBO_obj.Unbind();
    EBO_obj.Unbind();

    // --- САМОЕ ВАЖНОЕ ИСПРАВЛЕНИЕ ---
    VBOS.ID = instanceVBO;
    VBOS.target = GL_ARRAY_BUFFER; // Без этого SetData не работал!
}