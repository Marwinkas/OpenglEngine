#pragma once
#include <physx/PxPhysicsAPI.h>

using namespace physx; // Чтобы не писать physx:: каждый раз

class PhysicsEngine {
public:
    void Init();
    void Update(float deltaTime);
    void Cleanup();

    // Делаем их публичными, чтобы потом легко добавлять объекты из main
    PxPhysics* physics = nullptr;
    PxScene* scene = nullptr;

private:
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errorCallback;

    PxFoundation* foundation = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr; // Для многопоточности
};