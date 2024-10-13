#include "vk_mesh_assets.h"
#include <gtx/quaternion.inl>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include "../../vk_renderer.h"

std::optional<std::vector<std::shared_ptr<MeshAsset>>> LoadGltfMeshes(VkRenderer *renderer, const std::filesystem::path& path) {
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(data.error()));
        return std::nullopt;
    }

    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;

    fastgltf::Parser parser{};

    auto load = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
    if (load.error() != fastgltf::Error::None) {
        fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(load.error()));
        return std::nullopt;
    }

    fastgltf::Asset gltf = std::move(load.get());

    std::vector<std::shared_ptr<MeshAsset>> meshes;

    std::vector<uint16_t> indices;
    std::vector<VkVertex> vertices;

    for (auto &[primitives, weights, name] : gltf.meshes) {
        MeshAsset meshAsset;
        meshAsset.name = name;

        indices.clear();
        vertices.clear();

        for (auto &&primitive : primitives) {
            GeoSurface geoSurface{
                static_cast<uint32_t>(indices.size()),
                static_cast<uint32_t>(gltf.accessors[primitive.indicesAccessor.value()].count)
            };

            size_t initialVerticesSize = vertices.size();

            // Load indices
            {
                auto &indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);

                fastgltf::iterateAccessor<uint16_t>(gltf, indexAccessor, [&](auto value) {
                    indices.push_back(static_cast<uint16_t>(initialVerticesSize + value));
                });
            }

            // Load vertex positions
            {
                auto &posAccessor = gltf.accessors[primitive.findAttribute("POSITION")->accessorIndex];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](auto value, const size_t index) {
                    vertices[initialVerticesSize + index] = {
                        value,
                        {1, 0, 0},
                        glm::vec3 {1.f},
                        0.f,
                        0.f,
                    };
                });
            }

            // Load vertex normals
            if (auto normals = primitive.findAttribute("NORMAL"); normals != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex], [&](auto value, const size_t index) {
                    vertices[initialVerticesSize + index].normal = value;
                });
            }

            // Load tex coords
            if (auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex], [&](auto value, const size_t index) {
                    vertices[initialVerticesSize + index].uv_X = value.x;
                    vertices[initialVerticesSize + index].uv_Y = value.y;
                });
            }

            // Load vertex colors
            if (auto color = primitive.findAttribute("COLOR_0"); color != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[color->accessorIndex], [&](auto value, const size_t index) {
                    vertices[initialVerticesSize + index].color = value;
                });
            }

            meshAsset.surfaces.push_back(geoSurface);
        }
        constexpr bool OverrideColors = false;
        if (OverrideColors) {
            for (auto &[pos, normal, color, uv_X, uv_Y] : vertices) {
                color = glm::vec3(normal);
            }
        }

        meshAsset.mesh = renderer->CreateMesh(vertices, indices);
        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(meshAsset)));
    }

    return meshes;
}