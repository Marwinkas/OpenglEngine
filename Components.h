#pragma once
#include <entt/entt.hpp>
#include "Mesh.h"
#include "Light.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace physx { class PxRigidActor; } // Оставим пока PhysX, потом заменим на Jolt

// 1. Имя и теги (Всё, что нужно для UI и поиска)
struct TagComponent {
    std::string name = "Entity";
    std::vector<std::string> tags;
    std::vector<std::string> layers;
};

// 2. Координаты в пространстве
struct TransformComponent {
    Transform transform;
};

// 3. Визуал (если у сущности нет этого компонента, рендер её просто игнорирует)
struct MeshComponent {
    std::string modelPath = "";
    std::vector<std::string> materialPaths;
    MeshRenderer renderer;
    bool isStatic = false;
    bool isVisible = true;
    bool castShadow = true;
};

// 4. Освещение
struct LightComponent {
    Light light;
};

// 5. Иерархия (Родитель и дети теперь ссылаются на ID сущностей, а не на индексы массива)
struct HierarchyComponent {
    entt::entity parent = entt::null;
    std::vector<entt::entity> children;
};

// 6. Физика (Объединили коллайдер и тело в один плотный компонент)
enum class ColliderType { Box, Sphere, Plane };
enum class RigidBodyType { Static, Dynamic };

struct PhysicsComponent {
    ColliderType colliderType = ColliderType::Box;
    glm::vec3 extents = glm::vec3(1.0f, 1.0f, 1.0f);
    float radius = 1.0f;

    RigidBodyType bodyType = RigidBodyType::Dynamic;
    float mass = 10.0f;
    float friction = 0.5f;
    float restitution = 0.5f;

    physx::PxRigidActor* pxActor = nullptr;

    bool updatePhysicsTransform = false;
    bool rebuildPhysics = false;
};