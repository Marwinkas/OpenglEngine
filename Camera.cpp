#include "Camera.h"
#include "PhysicsEngine.h" // Подключаем физику именно здесь!
Camera::Camera(int width, int height, glm::vec3 position)
{
    Camera::width = width;
    Camera::height = height;
    Position = position;
}

void Camera::updateMatrix(float FOVdeg, float nearPlane, float farPlane)
{
    // Теперь мы вызываем правильную функцию GetViewMatrix()
    glm::mat4 view = GetViewMatrix();

    // Считаем матрицу с TAA тряской
    glm::mat4 projJitter = GetProjectionMatrix(FOVdeg, nearPlane, farPlane, true);
    viewProjectionMatrix = projJitter * view;

    // Считаем чистую матрицу
    glm::mat4 projClean = GetProjectionMatrix(FOVdeg, nearPlane, farPlane, false);
    cleanViewProjectionMatrix = projClean * view;
}

void Camera::Matrix(Shader& shader, const char* uniform)
{
    // Передаем новую правильную комбинированную матрицу
    glUniformMatrix4fv(glGetUniformLocation(shader.ID, uniform), 1, GL_FALSE, glm::value_ptr(viewProjectionMatrix));
}
bool Camera::IsSphereInFrustum(const glm::vec4* planes, const glm::vec3& center, float radius) {
    // Просто пробегаемся по готовым плоскостям
    for (int i = 0; i < 6; i++) {
        // Если центр сферы находится ЗА плоскостью дальше, чем её радиус - она невидима
        if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius) {
            return false;
        }
    }
    return true;
}
// --- СОЗДАНИЕ/УДАЛЕНИЕ ФИЗИКИ КАМЕРЫ ---
void Camera::TogglePlayMode(PhysicsEngine& physics) {
    isPlayMode = !isPlayMode;

    if (isPlayMode) {
        // ПЕРЕХОДИМ В ИГРУ: Создаем физическую капсулу вокруг камеры
        physx::PxMaterial* mat = physics.physics->createMaterial(0.5f, 0.5f, 0.0f); // Без прыгучести!

        // Капсула высотой 1.8м (радиус 0.4, половина высоты цилиндра 0.5)
        physx::PxShape* shape = physics.physics->createShape(physx::PxCapsuleGeometry(0.4f, 2.5f), *mat);

        // PhysX капсулы по умолчанию лежат на боку (по оси X). Поворачиваем её вертикально (по Y).
        physx::PxTransform relativePose(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 0, 1)));
        shape->setLocalPose(relativePose);

        physx::PxTransform startTransform(physx::PxVec3(Position.x, Position.y, Position.z));
        playerBody = physics.physics->createRigidDynamic(startTransform);
        playerBody->attachShape(*shape);
        physx::PxRigidBodyExt::updateMassAndInertia(*playerBody, 80.0f); // Вес человека 80 кг

        // Запрещаем капсуле падать набок (блокируем вращение физикой)
        playerBody->setRigidDynamicLockFlags(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
            physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y |
            physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);

        physics.scene->addActor(*playerBody);
        shape->release();
        mat->release();
        std::cout << "Режим ИГРЫ включен! Физика игрока создана." << std::endl;
    }
    else {
        // ПЕРЕХОДИМ В РЕДАКТОР: Удаляем физику
        if (playerBody) {
            physics.scene->removeActor(*playerBody);
            playerBody->release();
            playerBody = nullptr;
        }
        std::cout << "Режим РЕДАКТОРА включен! Свободный полет." << std::endl;
    }
}

// --- ОБНОВЛЕНИЕ ПОЗИЦИИ ИЗ PHYSX (Вызывать каждый кадр) ---
void Camera::UpdatePhysics(float deltaTime) {
    if (!isPlayMode || !playerBody) return;

    // 1. Получаем координаты капсулы из Матрицы
    physx::PxTransform transform = playerBody->getGlobalPose();

    // 2. Ставим камеру на уровне глаз (чуть выше центра капсулы)
    Position = glm::vec3(transform.p.x, transform.p.y + 0.6f, transform.p.z);

    // 3. Простейшая проверка: стоим ли мы на земле? 
    // (Если скорость по Y (вниз/вверх) почти нулевая, считаем что на полу)
    physx::PxVec3 velocity = playerBody->getLinearVelocity();
    isGrounded = (abs(velocity.y) < 0.1f);
}

// --- УПРАВЛЕНИЕ (Вызывать каждый кадр) ---
void Camera::Inputs(GLFWwindow* window, float deltaTime)
{
    // Управление мышью (Вращение головы) работает всегда, НО в редакторе только с зажатой ПКМ
    bool allowMouse = isPlayMode || (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

    if (allowMouse)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (firstClick) { lastMouseX = mouseX; lastMouseY = mouseY; firstClick = false; }

        float rotX = sensitivity * float(mouseY - lastMouseY);
        float rotY = sensitivity * float(mouseX - lastMouseX);
        lastMouseX = mouseX; lastMouseY = mouseY;

        // Вращение вверх/вниз
        glm::vec3 newOrientation = glm::rotate(Orientation, glm::radians(-rotX), glm::normalize(glm::cross(Orientation, Up)));
        if (abs(glm::angle(newOrientation, Up) - glm::radians(90.0f)) <= glm::radians(85.0f)) {
            Orientation = newOrientation;
        }
        // Вращение влево/вправо
        Orientation = glm::rotate(Orientation, glm::radians(-rotY), Up);
    }
    else
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        firstClick = true;
    }


    // --- ДВИЖЕНИЕ ---
    if (isPlayMode) {
        // РЕЖИМ ИГРЫ: Двигаем физическую капсулу (без полетов)
        if (!playerBody) return;

        // Направление движения (только по горизонтали, чтобы не летать в небо)
        glm::vec3 forward = glm::normalize(glm::vec3(Orientation.x, 0.0f, Orientation.z));
        glm::vec3 right = glm::normalize(glm::cross(forward, Up));

        glm::vec3 moveDir(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;

        if (glm::length(moveDir) > 0.1f) moveDir = glm::normalize(moveDir);

        // Получаем текущую физическую скорость
        physx::PxVec3 currentVel = playerBody->getLinearVelocity();

        // Меняем только скорости X и Z (чтобы гравитация по Y работала)
        currentVel.x = moveDir.x * walkSpeed;
        currentVel.z = moveDir.z * walkSpeed;

        // Прыжок (если стоим на земле)
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isGrounded) {
            currentVel.y = jumpForce;
        }

        // Применяем скорость к капсуле
        playerBody->setLinearVelocity(currentVel);
    }
    else
    {
        // РЕЖИМ РЕДАКТОРА (Unreal Engine Style)
        // Движение работает ТОЛЬКО если зажата правая кнопка мыши!
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {

            float currentSpeed = speed;
            // Ускорение на Shift
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) currentSpeed *= 4.0f;

            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) Position += currentSpeed * Orientation;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) Position += currentSpeed * -Orientation;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) Position += currentSpeed * -glm::normalize(glm::cross(Orientation, Up));
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) Position += currentSpeed * glm::normalize(glm::cross(Orientation, Up));

            // Взлет и спуск (E - вверх, Q - вниз)
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) Position += currentSpeed * Up;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) Position += currentSpeed * -Up;
        }
    }
}