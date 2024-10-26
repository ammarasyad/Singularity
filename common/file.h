#ifndef D3D12_STUFF_FILE_H
#define D3D12_STUFF_FILE_H

#include <fstream>
#include <vector>
#include <filesystem>

template<typename T>
static inline std::vector<T> ReadFile(const std::filesystem::path &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        // throw std::runtime_error("Failed to open file: " + filename);
        fprintf(stderr, "Failed to open file: %s\n", filename.c_str());
        return {};
    }

    const std::streamsize fileSize = file.tellg();
    std::vector<T> buffer(static_cast<size_t>(fileSize)); // originally char

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
    file.close();

    return std::move(buffer);
}

static inline void WriteFile(const std::filesystem::path &filename, const void *data, const std::streamsize size) {
    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char *>(data), size);
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

struct Bitmap {
    BitmapFileHeader fileHeader;
    BitmapInfoHeader infoHeader;
    BitmapColorHeader colorHeader;
    const char *data;
};

static inline void SaveToBitmap(const std::filesystem::path &filename, const char *data, const uint32_t width, const uint32_t height, const uint32_t rowPitch) {
    Bitmap bitmap{.data = data};

    bitmap.fileHeader.size = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + sizeof(BitmapColorHeader) + width * height * 4;
    bitmap.fileHeader.offset = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + sizeof(BitmapColorHeader);

    bitmap.infoHeader.size = sizeof(BitmapInfoHeader);
    bitmap.infoHeader.width = static_cast<int32_t>(width);
    bitmap.infoHeader.height = -static_cast<int32_t>(height);
    bitmap.infoHeader.sizeImage = width * height;

    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&bitmap.fileHeader), sizeof(BitmapFileHeader));
    file.write(reinterpret_cast<const char *>(&bitmap.infoHeader), sizeof(BitmapInfoHeader));
    file.write(reinterpret_cast<const char *>(&bitmap.colorHeader), sizeof(BitmapColorHeader));

//    for (uint32_t y = 0; y < height; y++) {
//        file.write(data + rowPitch * y, width * 4);
//    }
    file.write(data, width * height * 4);
    file.close();
}

#endif //D3D12_STUFF_FILE_H
