#include "TextureImporter.h"
#include <iostream>
#include <fstream>
#include <cmath>

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_dxt.h>
bool TextureImporter::ReadHeader(const std::string& filePath, BHTexHeader& outHeader) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    file.read(reinterpret_cast<char*>(&outHeader), sizeof(BHTexHeader));
    return true;
}

bool TextureImporter::UpdateHeader(const std::string& filePath, const BHTexHeader& newHeader) {
    // Открываем файл ОДНОВРЕМЕННО для чтения и записи (чтобы не стереть пиксели!)
    std::fstream file(filePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;

    file.seekp(0); // Прыгаем в самый нулевой байт файла
    file.write(reinterpret_cast<const char*>(&newHeader), sizeof(BHTexHeader)); // Перезаписываем только шапку
    return true;
}
bool TextureImporter::ImportTexture(const std::string& srcPath, const std::string& destPath) {
    int width, height, channels;

    // 1. Загружаем исходную картинку (всегда требуем 4 канала RGBA)
    unsigned char* data = stbi_load(srcPath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "[ERROR] TextureImporter: Failed to load " << srcPath << std::endl;
        return false;
    }

    // Определяем, нужна ли альфа-канал (DXT5) или хватит DXT1
    bool hasAlpha = (channels == 4);

    BHTexHeader header;
    header.width = width;
    header.height = height;
    header.format = hasAlpha ? 1 : 0; // 0 = DXT1, 1 = DXT5

    // Считаем количество мипмапов: 1 + floor(log2(max(w, h)))
    header.mipCount = 1 + static_cast<int>(std::floor(std::log2(std::max(width, height))));

    std::ofstream outFile(destPath, std::ios::binary);
    if (!outFile.is_open()) {
        stbi_image_free(data);
        return false;
    }

    // 2. Пишем заголовок
    outFile.write(reinterpret_cast<const char*>(&header), sizeof(BHTexHeader));

    int currentW = width;
    int currentH = height;
    unsigned char* currentData = data;
    std::vector<unsigned char> nextMipData;

    // 3. Генерируем и сжимаем мипмапы
    for (uint32_t mip = 0; mip < header.mipCount; ++mip) {

        // Сжимаем текущий мипмап в DXT и пишем в файл
        std::vector<unsigned char> compressedData;
        CompressToDXT(currentData, currentW, currentH, hasAlpha, compressedData);

        // Записываем размер сжатого блока и сам блок
        uint32_t dataSize = compressedData.size();
        outFile.write(reinterpret_cast<const char*>(&dataSize), sizeof(uint32_t));
        outFile.write(reinterpret_cast<const char*>(compressedData.data()), dataSize);

        // 4. Генерируем следующий мипмап (если это не последний)
        if (mip < header.mipCount - 1) {
            int nextW = std::max(1, currentW / 2);
            int nextH = std::max(1, currentH / 2);

            nextMipData.resize(nextW * nextH * 4);
            stbir_resize_uint8_linear(currentData, currentW, currentH, 0,
                nextMipData.data(), nextW, nextH, 0,
                (stbir_pixel_layout)4); // 4 = RGBA

            // Теперь 'currentData' указывает на наш новый уменьшенный буфер
            currentData = nextMipData.data();
            currentW = nextW;
            currentH = nextH;
        }
    }

    stbi_image_free(data);
    outFile.close();
    std::cout << "[SUCCESS] Imported texture: " << destPath << " (Mips: " << header.mipCount << ")" << std::endl;
    return true;
}

// Функция упаковки пикселей в блочный формат видеокарты
void TextureImporter::CompressToDXT(const unsigned char* rgbaData, int width, int height, bool hasAlpha, std::vector<unsigned char>& outData) {
    // DXT работает блоками 4x4 пикселя. 
    // Размер блока: DXT1 = 8 байт, DXT5 = 16 байт.
    int blockSize = hasAlpha ? 16 : 8;

    // Выравниваем размеры до кратности 4
    int paddedW = (width + 3) & ~3;
    int paddedH = (height + 3) & ~3;

    outData.resize((paddedW / 4) * (paddedH / 4) * blockSize);
    unsigned char* outPtr = outData.data();

    // Временный буфер для извлечения блока 4x4
    unsigned char block[64]; // 4 * 4 * 4(RGBA) = 64 байта

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            // Копируем пиксели в блок 4x4
            for (int by = 0; by < 4; ++by) {
                for (int bx = 0; bx < 4; ++bx) {
                    int srcX = std::min(x + bx, width - 1);
                    int srcY = std::min(y + by, height - 1);
                    int srcIdx = (srcY * width + srcX) * 4;
                    int dstIdx = (by * 4 + bx) * 4;

                    block[dstIdx] = rgbaData[srcIdx];
                    block[dstIdx + 1] = rgbaData[srcIdx + 1];
                    block[dstIdx + 2] = rgbaData[srcIdx + 2];
                    block[dstIdx + 3] = rgbaData[srcIdx + 3];
                }
            }

            // Сжимаем блок 4x4
            stb_compress_dxt_block(outPtr, block, hasAlpha ? 1 : 0, STB_DXT_NORMAL);
            outPtr += blockSize;
        }
    }
}