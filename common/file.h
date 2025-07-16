#ifndef D3D12_STUFF_FILE_H
#define D3D12_STUFF_FILE_H

#include <fstream>
#include <vector>
#include <filesystem>
#include <immintrin.h>

template<typename T>
std::vector<T> ReadFile(const std::filesystem::path &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
#ifdef _WIN32
#define FORMAT_SPECIFIER "%ls"
#else
#define FORMAT_SPECIFIER "%s"
#endif
        fprintf(stderr, "Failed to open file: " FORMAT_SPECIFIER "\n", filename.c_str());
        return {};
    }

    const std::streamsize fileSize = file.tellg();
    std::vector<T> buffer(static_cast<size_t>(fileSize)); // originally char

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
    file.close();

    return std::move(buffer);
}

inline void WriteFile(const std::filesystem::path &filename, const void *data, const std::streamsize size) {
    std::ofstream file(filename, std::ios::binary);
    file.write(static_cast<const char *>(data), size);
    file.close();
}

struct __attribute__((packed)) BitmapFileHeader {
    uint16_t type{0x4D42};
    uint32_t size{0};
    uint16_t reserved1{0};
    uint16_t reserved2{0};
    uint32_t offset{0};
};

struct __attribute__((packed)) BitmapInfoHeader {
    uint32_t size{0};
    int32_t  width{0};
    int32_t  height{0};

    uint16_t planes{1};
    uint16_t bitCount{32};
    uint32_t compression{0};
    uint32_t sizeImage{0};
    int32_t  xPixelsPerMeter{0};
    int32_t  yPixelsPerMeter{0};
    uint32_t colorsUsed{0};
    uint32_t colorsImportant{0};
};

struct __attribute__((packed)) BitmapColorHeader {
    uint32_t redMask{0x00FF0000};
    uint32_t greenMask{0x0000FF00};
    uint32_t blueMask{0x000000FF};
    uint32_t alphaMask{0xFF000000};
    uint32_t colorSpaceType{0x73524742}; // sRGB
    uint32_t unused[16]{0};
};

struct BitmapHeader {
    BitmapFileHeader fileHeader;
    BitmapInfoHeader infoHeader;
    BitmapColorHeader colorHeader;
};

static inline void SaveToBitmap(const std::filesystem::path &filename, char *data, const uint32_t width, const uint32_t height, const uint32_t rowPitch) {
    BitmapHeader bitmap{};

    bitmap.fileHeader.size = sizeof(BitmapHeader) + width * height;
    bitmap.fileHeader.offset = sizeof(BitmapHeader);

    bitmap.infoHeader.size = sizeof(BitmapInfoHeader);
    bitmap.infoHeader.width = static_cast<int32_t>(width);
    bitmap.infoHeader.height = -static_cast<int32_t>(height);
    bitmap.infoHeader.sizeImage = width * height;

    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&bitmap.fileHeader), sizeof(BitmapFileHeader));
    file.write(reinterpret_cast<const char *>(&bitmap.infoHeader), sizeof(BitmapInfoHeader));
    file.write(reinterpret_cast<const char *>(&bitmap.colorHeader), sizeof(BitmapColorHeader));

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < rowPitch; x += 32) {
            __m256i bgra = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + y * rowPitch + x));

            // Curse you Intel for removing AVX512
            const __m256i mask = _mm256_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15, 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
            const __m256i rgba = _mm256_shuffle_epi8(bgra, mask);

            file.write(reinterpret_cast<const char *>(&rgba), 32);
        }
    }
    file.close();
}

#endif //D3D12_STUFF_FILE_H
