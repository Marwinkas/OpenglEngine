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
        glGenTextures(1, &task->textureID);
        glBindTexture(GL_TEXTURE_2D, task->textureID);

        // ЖЕСТКО СТАВИМ ПРАВИЛЬНЫЕ ФИЛЬТРЫ (Перебиваем битые настройки JSON)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, task->header.wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, task->header.wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // ВАЖНО: АНИЗОТРОПНАЯ ФИЛЬТРАЦИЯ (УБИВАЕТ МЫЛО ПОД УГЛОМ НА ПОЛУ)
        float maxAniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        if (maxAniso > 0.0f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
        }

        GLenum glFormat = (task->header.format == 1) ?
            (task->header.isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) :
            (task->header.isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);

        // Резервируем память под ВСЕ мипы сразу
        glTexStorage2D(GL_TEXTURE_2D, task->header.mipCount, glFormat, task->header.width, task->header.height);

        // Грузим ВСЕ мипмапы за один проход! (С PBO это безопасно и быстро)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        for (uint32_t m = 0; m < task->header.mipCount; m++) {
            UploadSingleMip(task, m);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        // Создаем резидентный хэндл ОДИН РАЗ, без изменения Base Level
        task->bindlessHandle = glGetTextureHandleARB(task->textureID);
        glMakeTextureHandleResidentARB(task->bindlessHandle);

        task->targetTexture->ID = task->textureID;
        task->targetTexture->handle = task->bindlessHandle;

        std::cout << "[Стриминг] Текстура загружена ИДЕАЛЬНО: " << task->filepath << std::endl;

        std::lock_guard<std::mutex> lock(queueMutex);
        readyQueue.pop();
        delete task;

        return true; // Дергаем SSBO (isSceneDirty = true в main)
    }
    return false;
}