#ifndef D3D12_STUFF_FILE_H
#define D3D12_STUFF_FILE_H

#include <fstream>
#include <vector>

template<typename T>
static std::vector<T> ReadFile(const std::string &filename) {
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
#endif //D3D12_STUFF_FILE_H
