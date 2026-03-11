#include "PhysicsEngine.h"
#include <iostream>

void PhysicsEngine::Init() {
    // 1. Создаём базу (Фундамент)
    foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
    if (!foundation) {
        std::cerr << "Критическая ошибка: не удалось создать PhysX Foundation!" << std::endl;
        return;
    }

    // 2. Инициализируем само ядро физики
    physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), true, nullptr);
    if (!physics) {
        std::cerr << "Критическая ошибка: не удалось создать PxPhysics!" << std::endl;
        return;
    }

    // 3. Создаём физическую сцену (наш мир)
    PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f); // Гравитация тянет вниз по оси Y

    // Выделяем 4 потока процессора под физику (у тебя их с запасом!)
    dispatcher = PxDefaultCpuDispatcherCreate(4);
    sceneDesc.cpuDispatcher = dispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader; // Стандартные правила столкновений

    scene = physics->createScene(sceneDesc);

    std::cout << "PhysX успешно запущен и готов к работе!" << std::endl;
}

void PhysicsEngine::Update(float deltaTime) {
    if (scene) {
        // Делаем шаг симуляции. 
        // Важно: PhysX любит фиксированный шаг (например, 1/60 секунды), 
        // но для начала мы просто передадим время между кадрами.
        scene->simulate(deltaTime);
        scene->fetchResults(true); // Ждём, пока физика дочитается
    }
}

void PhysicsEngine::Cleanup() {
    // Правило хорошего тона: удаляем всё в обратном порядке
    if (scene) scene->release();
    if (dispatcher) dispatcher->release();
    if (physics) physics->release();
    if (foundation) foundation->release();
}