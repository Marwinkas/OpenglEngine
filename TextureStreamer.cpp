#include "TextureStreamer.h"
#include <fstream>
#include <iostream>

TextureStreamer::TextureStreamer() {
    glGenBuffers(1, &pbo);
    isRunning = true;
    workerThread = std::thread(&TextureStreamer::WorkerThreadLoop, this);
}

TextureStreamer::~TextureStreamer() {
    isRunning = false;
    if (workerThread.joinable()) workerThread.join();
    glDeleteBuffers(1, &pbo);
}

void TextureStreamer::StreamTextureAsync(const std::string& filepath, Texture* outTexture) {
    TextureLoadTask* task = new TextureLoadTask();
    task->filepath = filepath;
    task->targetTexture = outTexture;

    std::lock_guard<std::mutex> lock(queueMutex);
    taskQueue.push(task);
}

void TextureStreamer::WorkerThreadLoop() {
    while (isRunning) {
        TextureLoadTask* currentTask = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!taskQueue.empty()) {
                currentTask = taskQueue.front();
                taskQueue.pop();
            }
        }

        if (currentTask != nullptr) {
            std::ifstream file(currentTask->filepath, std::ios::binary);
            if (file.is_open()) {
                file.read(reinterpret_cast<char*>(&currentTask->header), sizeof(BHTexHeader));

                size_t currentOffset = 0;
                for (uint32_t i = 0; i < currentTask->header.mipCount; ++i) {
                    uint32_t dataSize;
                    file.read(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));

                    currentTask->mipSizes.push_back(dataSize);
                    currentTask->mipOffsets.push_back(currentOffset); // Запоминаем, где лежит этот мип

                    currentOffset += dataSize;

                    size_t curVecSize = currentTask->rawData.size();
                    currentTask->rawData.resize(curVecSize + dataSize);
                    file.read(currentTask->rawData.data() + curVecSize, dataSize);
                }
                file.close();

                // Начинаем загрузку с САМОГО МАЛЕНЬКОГО мипмапа (последнего)
                currentTask->currentMipToUpload = currentTask->header.mipCount - 1;

                std::lock_guard<std::mutex> lock(queueMutex);
                readyQueue.push(currentTask);
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// Вспомогательная функция для заливки одного уровня
void TextureStreamer::UploadSingleMip(TextureLoadTask* task, int mipLevel) {
    size_t size = task->mipSizes[mipLevel];
    size_t offset = task->mipOffsets[mipLevel];

    // Вычисляем ширину и высоту именно для этого мипмапа (сдвиг битов вправо == деление на 2)
    int w = std::max(1, (int)task->header.width >> mipLevel);
    int h = std::max(1, (int)task->header.height >> mipLevel);

    GLenum glFormat = (task->header.format == 1) ?
        (task->header.isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) :
        (task->header.isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);

    // Заливаем кусок данных из оперативки в PBO
    glBufferData(GL_PIXEL_UNPACK_BUFFER, size, task->rawData.data() + offset, GL_STREAM_DRAW);

    // Копируем из PBO в видеокарту (работает АСИНХРОННО!)
    glCompressedTexSubImage2D(GL_TEXTURE_2D, mipLevel, 0, 0, w, h, glFormat, size, 0);
}

// Вызывается каждый кадр в main.cpp
bool TextureStreamer::Update() {
    TextureLoadTask* task = nullptr;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (!readyQueue.empty()) {
            task = readyQueue.front();
        }
    }

    if (task != nullptr) {
        // ЭТАП 1: Выделяем память и загружаем "мыло" (Первый кадр)
        if (!task->isStorageAllocated) {
            glGenTextures(1, &task->textureID);
            glBindTexture(GL_TEXTURE_2D, task->textureID);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, task->header.wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, task->header.wrapT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, task->header.minFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, task->header.magFilter);

            GLenum glFormat = (task->header.format == 1) ?
                (task->header.isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) :
                (task->header.isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);

            // Резервируем память под ВСЕ мипы сразу
            glTexStorage2D(GL_TEXTURE_2D, task->header.mipCount, glFormat, task->header.width, task->header.height);

            // =========================================================
            // ФИКС: Ограничиваем текстуру только теми мипами, которые есть
            // =========================================================
            int startMip = std::min((int)task->header.mipCount - 1, 4); // Грузим с 4-го (или последнего, если текстура мелкая)

            // Говорим OpenGL временно считать базовым уровнем наш мыльный мип!
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, startMip);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, task->header.mipCount - 1);
            // =========================================================

            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            task->currentMipToUpload = startMip;
            while (task->currentMipToUpload >= 0) {
                // Если мы дошли до 3-го мипа, останавливаем синхронную загрузку,
                // остальное догрузим асинхронно в следующих кадрах!
                if (task->currentMipToUpload < startMip - 2) break;

                UploadSingleMip(task, task->currentMipToUpload);
                task->currentMipToUpload--;
            }
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

            task->bindlessHandle = glGetTextureHandleARB(task->textureID);
            glMakeTextureHandleResidentARB(task->bindlessHandle);

            task->targetTexture->ID = task->textureID;
            task->targetTexture->handle = task->bindlessHandle;

            task->isStorageAllocated = true;
            return true; // Обновляем SSBO!
        }
        // ЭТАП 2: Догружаем тяжелые мипы
        else {
            if (task->currentMipToUpload >= 0) {
                glBindTexture(GL_TEXTURE_2D, task->textureID);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);

                UploadSingleMip(task, task->currentMipToUpload);

                // =========================================================
                // ФИКС: По мере загрузки больших мипов, сдвигаем Base Level!
                // Это заставит текстуру становиться резче прямо на глазах!
                // =========================================================
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, task->currentMipToUpload);

                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                task->currentMipToUpload--;
            }

            if (task->currentMipToUpload < 0) {
                std::cout << "[Стриминг] Текстура ПОЛНОСТЬЮ прогружена: " << task->filepath << std::endl;

                // Возвращаем настройки в нормальное состояние
                glBindTexture(GL_TEXTURE_2D, task->textureID);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                std::lock_guard<std::mutex> lock(queueMutex);
                readyQueue.pop();
                delete task;
            }
            return false;
        }
    }
    return false;
}