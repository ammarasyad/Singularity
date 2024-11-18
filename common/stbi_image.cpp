#ifndef NDEBUG
#define STBI_ASSERT(x)
#endif

#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STBI_NO_LINEAR

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>