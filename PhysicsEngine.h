#ifndef PHYSICSENGINE_CLASS_H
#define PHYSICSENGINE_CLASS_H
#include <physx/PxPhysicsAPI.h>
#include <vector>
#include "GameObject.h"
using namespace physx; // Чтобы не писать physx:: каждый раз

class PhysicsEngine {
public:
    void Init();
    void Update(float deltaTime);
    void Cleanup();
    void ApplyUIChanges(std::vector<GameObject>& objects);
    void CreateTestScene();
    void RegisterObjects(std::vector<GameObject>& objects);
    void SyncTransforms(std::vector<GameObject>& objects);
    PxMaterial* defaultMaterial = nullptr;
    PxRigidDynamic* testBox = nullptr; // Наш физический кубик

    // Делаем их публичными, чтобы потом легко добавлять объекты из main
    PxPhysics* physics = nullptr;
    PxScene* scene = nullptr;

private:
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errorCallback;

    PxFoundation* foundation = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr; // Для многопоточности


};
#endif