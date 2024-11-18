cmake_minimum_required(VERSION 3.30)
project(Singularity)

#if(CMAKE_CXX_COMPILER MATCHES "GNU")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=full")
#endif()

set(CMAKE_CXX_STANDARD 23)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GLFW_BUILD_X11 ON)
    set(GLFW_BUILD_WAYLAND ON)
endif ()

option(FASTGLTF_COMPILE_AS_CPP20 ON)
option(COMPILE_AVX2 "Compile with AVX2" ON)

add_subdirectory(third_party/glfw-3.4)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(third_party/D3D12MemoryAllocator)
endif()

add_subdirectory(third_party/VulkanMemoryAllocator)

find_package(Vulkan REQUIRED)

add_library(KTX SHARED IMPORTED GLOBAL)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(KTX_INCLUDE_DIR third_party/ktx/windows/include)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} third_party/ktx/windows/lib/cmake/ktx/)
    set_target_properties(KTX PROPERTIES IMPORTED_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ktx/windows/lib/ktx.lib)
else()
    set(KTX_INCLUDE_DIR third_party/ktx/linux/include)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} third_party/ktx/linux/lib/cmake/ktx/)
#    set(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/third_party/ktx/linux/lib/cmake/ktx/")
    set_target_properties(KTX PROPERTIES IMPORTED_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ktx/linux/lib/libktx.so)
endif()

find_package(KTX CONFIG REQUIRED CONFIGS KtxConfig.cmake)

set(glm "third_party/glm")
add_library(glm SHARED IMPORTED)

add_subdirectory(third_party/fastgltf)
add_subdirectory(third_party/meshoptimizer)

set(imgui "third_party/imgui")
file(GLOB IMGUI_GLOB ${imgui}/*.cpp ${imgui}/backends/imgui_impl_glfw.cpp $<$<PLATFORM_ID:Windows>:${imgui}/backends/imgui_impl_dx12.cpp> ${imgui}/backends/imgui_impl_vulkan.cpp)
add_library(imgui STATIC ${IMGUI_GLOB})
target_include_directories(imgui PUBLIC ${imgui} ${imgui}/backends)
target_link_libraries(imgui PRIVATE glfw Vulkan::Vulkan)

include_directories(${glm} ${glfw}/include ${KTX_INCLUDE_DIR} $<$<PLATFORM_ID:Windows>:${D3D12MemoryAllocator}/include> common graphics engine third_party/stb #[[third_party/png++]])

add_executable(Singularity main.cpp
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
        engine/objects/meshlets.cpp
        engine/objects/meshlets.h
)

set_target_properties(Singularity PROPERTIES WIN32_EXECUTABLE $<PLATFORM_ID:Windows>)

add_custom_target(shaders)

#file(GLOB SHADER_FILES graphics/vk/shaders/*.vert graphics/vk/shaders/*.frag graphics/vk/shaders/*.comp)
set(VK_SHADER_FOLDER graphics/vk/shaders)
set(SHADER_FILES
        ${VK_SHADER_FOLDER}/mesh/meshshader.frag
        ${VK_SHADER_FOLDER}/mesh/meshshader.mesh
        ${VK_SHADER_FOLDER}/mesh/meshshader.task
        ${VK_SHADER_FOLDER}/depth_prepass.vert
        ${VK_SHADER_FOLDER}/frustum.comp
        ${VK_SHADER_FOLDER}/light_culling.comp
        ${VK_SHADER_FOLDER}/lighting.frag
        ${VK_SHADER_FOLDER}/mesh.frag
        ${VK_SHADER_FOLDER}/mesh.vert
        ${VK_SHADER_FOLDER}/shadowmap.vert
        ${VK_SHADER_FOLDER}/shadowmap.frag
        ${VK_SHADER_FOLDER}/skybox.frag
        ${VK_SHADER_FOLDER}/skybox.vert
)

foreach (FILE ${SHADER_FILES})
    get_filename_component(FILE_NAME ${FILE} NAME)
    if (CMAKE_HOST_SYSTEM STREQUAL "Windows")
        add_custom_command(
                TARGET shaders
                COMMAND third_party/shaderc/bin/glslc.exe ${FILE} --target-spv=spv1.6 -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${FILE_NAME}.spv -g
                MAIN_DEPENDENCY ${FILE}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                VERBATIM
        )
    else ()
        add_custom_command(
                TARGET shaders
                COMMAND glslc ${FILE} --target-spv=spv1.6 -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${FILE_NAME}.spv -g
                MAIN_DEPENDENCY ${FILE}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                VERBATIM
        )
    endif ()
endforeach ()

if (COMPILE_AVX2)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2 -mfma")
endif ()

target_compile_options(Singularity PRIVATE -fopenmp)

add_dependencies(Singularity shaders)
target_include_directories(Singularity PRIVATE imgui third_party/glm third_party/fastgltf/include #[[third_party/libpng]])
target_link_options(Singularity PRIVATE -fopenmp $<$<PLATFORM_ID:Windows>:-static>)

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(Singularity PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib D3D12MemoryAllocator)
endif ()

target_link_libraries(Singularity PRIVATE glfw fastgltf imgui meshoptimizer Vulkan::Vulkan KTX::ktx GPUOpen::VulkanMemoryAllocator)
