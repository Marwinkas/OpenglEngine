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
void PhysicsEngine::CreateTestScene() {
    // 1. Создаем материал (Статическое трение, Динамическое трение, Упругость/Прыгучесть)
    defaultMaterial = physics->createMaterial(0.5f, 0.5f, 0.6f);

    // 2. Создаем невидимый пол (Бесконечная плоскость)
    // Нормаль смотрит вверх (0, 1, 0), высота 0
    PxRigidStatic* groundPlane = PxCreatePlane(*physics, PxPlane(0.0f, 1.0f, 0.0f, 0.0f), *defaultMaterial);
    scene->addActor(*groundPlane);

    // 3. Создаем падающий кубик
    // Спавним его на высоте 10 метров
    PxTransform boxTransform(PxVec3(0.0f, 10.0f, 0.0f));
    testBox = physics->createRigidDynamic(boxTransform);

    // Задаем форму. В PhysX размеры задаются как "половинки" (Half-Extents).
    // PxBoxGeometry(1, 1, 1) означает куб размером 2x2x2 метра.
    PxShape* boxShape = physics->createShape(PxBoxGeometry(1.0f, 1.0f, 1.0f), *defaultMaterial);
    testBox->attachShape(*boxShape);
    boxShape->release(); // Форма прикрепилась, счетчик ссылок можно сбросить

    // Считаем массу (допустим, плотность 10 кг/м3)
    PxRigidBodyExt::updateMassAndInertia(*testBox, 10.0f);

    scene->addActor(*testBox);
    std::cout << "Тестовая сцена PhysX загружена!" << std::endl;
}
void PhysicsEngine::Cleanup() {
    // Правило хорошего тона: удаляем всё в обратном порядке
    if (scene) scene->release();
    if (dispatcher) dispatcher->release();
    if (physics) physics->release();
    if (foundation) foundation->release();
}
void PhysicsEngine::RegisterObjects(std::vector<GameObject>& objects) {
   
}
void PhysicsEngine::ApplyUIChanges(std::vector<GameObject>& objects) {
   
}
void PhysicsEngine::SyncTransforms(std::vector<GameObject>& objects) {
   
}