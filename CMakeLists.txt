cmake_minimum_required(VERSION 3.29)
project(Singularity)

#if(CMAKE_CXX_COMPILER MATCHES "GNU")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=full")
#endif()

set(CMAKE_CXX_STANDARD 23)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

option(FASTGLTF_COMPILE_AS_CPP20 ON)
option(COMPILE_AVX2 "Compile with AVX2" ON)

set(libktx_DIR third_party/ktx/lib/cmake/ktx)
set(libktx_INCLUDE_DIR third_party/ktx/include)

add_subdirectory(third_party/glfw-3.4)
add_subdirectory(third_party/D3D12MemoryAllocator)
add_subdirectory(third_party/VulkanMemoryAllocator)

find_package(Vulkan REQUIRED)

find_package(libktx CONFIG REQUIRED CONFIGS KtxConfig.cmake)
include_directories(${libktx_INCLUDE_DIR})
add_library(libktx SHARED IMPORTED GLOBAL)
set_target_properties(libktx PROPERTIES IMPORTED_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ktx/lib/ktx.lib)

set(glm "third_party/glm")
add_library(glm SHARED IMPORTED)

add_subdirectory(third_party/fastgltf)

set(imgui "third_party/imgui")
file(GLOB IMGUI_GLOB ${imgui}/*.cpp ${imgui}/backends/imgui_impl_glfw.cpp ${imgui}/backends/imgui_impl_dx12.cpp ${imgui}/backends/imgui_impl_vulkan.cpp)
add_library(imgui STATIC ${IMGUI_GLOB})
target_include_directories(imgui PUBLIC ${imgui} ${imgui}/backends)
target_link_libraries(imgui PRIVATE glfw Vulkan::Vulkan)

include_directories(${glm} ${glfw}/include ${D3D12MemoryAllocator}/include common graphics engine third_party/stb)

add_executable(Singularity WIN32 main.cpp
        graphics/d3d12_renderer.cpp
        graphics/vk_renderer.cpp
        graphics/vk_renderer.h
        common/file.h
        graphics/vk/memory/vk_memory.cpp
        graphics/vk/memory/vk_memory.h
        graphics/vk/memory/vma_usage.cpp
        engine/camera.cpp
        engine/camera.h
        graphics/vk/vk_descriptor_layout.h
        graphics/vk/vk_common.h
        graphics/vk/vk_gui.cpp
        graphics/vk/vk_gui.h
        graphics/vk/memory/vk_mesh_assets.cpp
        graphics/vk/memory/vk_mesh_assets.h
        graphics/vk/vk_descriptor_layout.cpp
        engine/objects/render_object.h
        engine/objects/material.cpp
        engine/objects/material.h
        graphics/vk/vk_pipeline_builder.h
        graphics/vk/vk_pipeline_builder.cpp
        engine/objects/gltf.cpp
        engine/objects/gltf.h
        common/stbi_image.cpp
)

add_custom_target(shaders)

#file(GLOB SHADER_FILES graphics/vk/shaders/*.vert graphics/vk/shaders/*.frag graphics/vk/shaders/*.comp)
set(VK_SHADER_FOLDER graphics/vk/shaders)
set(SHADER_FILES
        ${VK_SHADER_FOLDER}/depth_prepass.vert
        ${VK_SHADER_FOLDER}/frustum.comp
        ${VK_SHADER_FOLDER}/light_culling.comp
        ${VK_SHADER_FOLDER}/lighting.frag
        ${VK_SHADER_FOLDER}/mesh.frag
        ${VK_SHADER_FOLDER}/mesh.vert
        ${VK_SHADER_FOLDER}/skybox.frag
        ${VK_SHADER_FOLDER}/skybox.vert
)

foreach (FILE ${SHADER_FILES})
    get_filename_component(FILE_NAME ${FILE} NAME)
    add_custom_command(
            TARGET shaders
            COMMAND third_party/shaderc/bin/glslc.exe ${FILE} -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${FILE_NAME}.spv -g
            MAIN_DEPENDENCY ${FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            VERBATIM
    )
endforeach ()

if (COMPILE_AVX2)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
endif ()

add_dependencies(Singularity shaders)
target_include_directories(Singularity PRIVATE imgui third_party/glm third_party/fastgltf/include)
target_link_options(Singularity PRIVATE -static-libstdc++)
target_link_libraries(Singularity PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib glfw fastgltf imgui D3D12MemoryAllocator Vulkan::Vulkan libktx GPUOpen::VulkanMemoryAllocator)
