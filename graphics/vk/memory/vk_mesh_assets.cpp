#include "vk_mesh_assets.h"
#include <gtx/quaternion.inl>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include "graphics/vk_renderer.h"

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

    std::vector<uint32_t> indices;
    std::vector<VkVertex> vertices;

    for (auto &[primitives, weights, name] : gltf.meshes) {
        MeshAsset meshAsset;

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

                fastgltf::iterateAccessor<uint32_t>(gltf, indexAccessor, [&](uint32_t value) {
                    indices.push_back(static_cast<uint32_t>(initialVerticesSize + value));
                });
            }

            // Load vertex positions
            {
                auto &posAccessor = gltf.accessors[primitive.findAttribute("POSITION")->accessorIndex];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](glm::vec3 value, const size_t index) {
                    vertices[initialVerticesSize + index] = {
                        {value, 1.0f},
                        {1, 0, 0, 0}
                    };
                });
            }

            // Load vertex normals
            if (auto normals = primitive.findAttribute("NORMAL"); normals != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex], [&](glm::vec3 value, const size_t index) {
                    vertices[initialVerticesSize + index].normal = {value, 0.f};
                });
            }

            // Load tex coords
            if (auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex], [&](glm::vec2 value, const size_t index) {
                    const auto u = value.x;
                    const auto v = value.y;
                    vertices[initialVerticesSize + index].pos.w = u;
                    vertices[initialVerticesSize + index].normal.w = v;
                });
            }

            meshAsset.surfaces.push_back(geoSurface);
        }

        meshAsset.mesh = renderer->CreateMesh(vertices, indices);
        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(meshAsset)));
    }

    return meshes;
}