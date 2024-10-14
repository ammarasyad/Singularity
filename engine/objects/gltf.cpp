//
// Created by Ammar on 13/10/2024.
//

#include "gltf.h"

#include <iostream>
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <stb_image.h>
#include <ranges>

#include "vk_renderer.h"
#include "detail/type_quat.hpp"
#include "gtc/quaternion.hpp"

inline std::optional<VulkanImage> loadImage(VkRenderer *renderer, fastgltf::Asset &asset, fastgltf::Image &image) {
    VulkanImage vulkanImage{};

    int width, height, channels;

    std::visit(
        fastgltf::visitor {
            [](auto &arg) {},
            [&](fastgltf::sources::URI &filePath) {
                assert(filePath.fileByteOffset == 0);
                assert(filePath.uri.isLocalPath());

                const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
                uint8_t *data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
                if (data) {
                    VkExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

                    vulkanImage = renderer->memory_manager()->createTexture(data, renderer, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::Array &array) {
                uint8_t *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(&array.bytes), static_cast<int>(array.bytes.size_bytes()), &width, &height, &channels, STBI_rgb_alpha);
                if (data) {
                    VkExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

                    vulkanImage = renderer->memory_manager()->createTexture(data, renderer, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::BufferView &bufferView) {
                assert(bufferView.bufferViewIndex < asset.bufferViews.size());
                const auto &view = asset.bufferViews[bufferView.bufferViewIndex];
                auto &buffer = asset.buffers[view.bufferIndex];

                std::visit(fastgltf::visitor {
                    [](auto &arg) {},
                    [&](fastgltf::sources::Array &array) {
                        uint8_t *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(array.bytes.data()) + view.byteOffset, static_cast<int>(view.byteLength), &width, &height, &channels, STBI_rgb_alpha);
                        if (data) {
                            VkExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

                            vulkanImage = renderer->memory_manager()->createTexture(data, renderer, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                            stbi_image_free(data);
                        }
                    }
                }, buffer.data);
            }
        }, image.data);

    return vulkanImage.image == VK_NULL_HANDLE ? std::nullopt : std::make_optional(vulkanImage);
}

inline VkFilter extractFilter(const fastgltf::Filter filter) {
    switch (filter) {
        case fastgltf::Filter::Linear:
        case fastgltf::Filter::LinearMipMapLinear:
        case fastgltf::Filter::LinearMipMapNearest:
            return VK_FILTER_LINEAR;

        case fastgltf::Filter::Nearest:
        case fastgltf::Filter::NearestMipMapLinear:
        case fastgltf::Filter::NearestMipMapNearest:
            return VK_FILTER_NEAREST;
        default:
            return VK_FILTER_LINEAR;
    }
}

inline VkSamplerMipmapMode extractMipmapMode(const fastgltf::Filter filter) {
    switch (filter) {
        case fastgltf::Filter::LinearMipMapLinear:
        case fastgltf::Filter::NearestMipMapLinear:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;

        case fastgltf::Filter::LinearMipMapNearest:
        case fastgltf::Filter::NearestMipMapNearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

//#pragma GCC push_options
//#pragma GCC optimize("O0")
std::optional<std::shared_ptr<LoadedGLTF>> LoadGLTF(VkRenderer *renderer, const std::filesystem::path &path) {
    auto scene = std::make_shared<LoadedGLTF>();
    scene->renderer = renderer;

//    LoadedGLTF &file = *scene;

    fastgltf::Parser parser{};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadExternalBuffers;

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(data.error()));
        return std::nullopt;
    }

    fastgltf::Asset gltf;

    switch (determineGltfFileType(data.get())) {
        case fastgltf::GltfType::glTF: {
            auto load = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
            if (load.error() != fastgltf::Error::None) {
                fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(load.error()));
                return std::nullopt;
            }

            gltf = std::move(load.get());
            break;
        }
        case fastgltf::GltfType::GLB: {
            auto load = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
            if (load.error() != fastgltf::Error::None) {
                fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(load.error()));
                return std::nullopt;
            }

            gltf = std::move(load.get());
            break;
        }
        case fastgltf::GltfType::Invalid:
            std::cerr << "Unknown gltf file type" << std::endl;
            return std::nullopt;
    }

    std::array<DescriptorAllocator::PoolSizeRatio, 3> sizes = {
        {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
        }
    };

    scene->descriptorAllocator.InitPool(renderer->logical_device(), gltf.materials.size(), sizes);

    scene->samplers.reserve(gltf.samplers.size());
    for (const auto &[magFilter, minFilter, wrapS, wrapT, name] : gltf.samplers) {
        VkSamplerCreateInfo samplerCreateInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerCreateInfo.magFilter = extractFilter(magFilter.value_or(fastgltf::Filter::Nearest));
        samplerCreateInfo.minFilter = extractFilter(minFilter.value_or(fastgltf::Filter::Nearest));

        samplerCreateInfo.minLod = 0.f;
        samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;

        samplerCreateInfo.mipmapMode = extractMipmapMode(minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        if (vkCreateSampler(renderer->logical_device(), &samplerCreateInfo, nullptr, &newSampler) != VK_SUCCESS) {
            std::cerr << "Failed to create sampler" << std::endl;
            return std::nullopt;
        }

        scene->samplers.push_back(newSampler);
    }

//    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<MeshAsset> meshes;
    meshes.reserve(gltf.meshes.size());
    std::vector<std::shared_ptr<Node>> nodes;
    nodes.reserve(gltf.nodes.size());
    std::vector<VulkanImage> images;
    images.reserve(gltf.images.size());
    std::vector<GLTFMaterial> materials;
    materials.reserve(gltf.materials.size());

    for (auto &image : gltf.images) {
        auto img = loadImage(renderer, gltf, image);

        if (img.has_value()) {
            images.push_back(img.value());
            scene->images[image.name.c_str()] = img.value();
        } else {
            std::cout << "Failed to load image: " << image.name << std::endl;
            images.push_back(renderer->default_image());
        }
    }
    VkBufferCreateInfo bufferCreateInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        sizeof(VkGLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };

    VmaAllocationCreateInfo allocationCreateInfo{
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        VMA_MEMORY_USAGE_AUTO
    };

    scene->materialDataBuffer = renderer->memory_manager()->createManagedBuffer(&bufferCreateInfo, &allocationCreateInfo);

    VkGLTFMetallic_Roughness::MaterialConstants *materialConstants;
    renderer->memory_manager()->mapBuffer(&scene->materialDataBuffer.buffer, reinterpret_cast<void **>(&materialConstants));

    int dataIndex = 0;

    for (auto &material : gltf.materials) {
        GLTFMaterial newMaterial{};
        scene->materials[material.name.c_str()] = newMaterial;

        VkGLTFMetallic_Roughness::MaterialConstants constants{};
        constants.colorFactors.x = material.pbrData.baseColorFactor[0];
        constants.colorFactors.y = material.pbrData.baseColorFactor[1];
        constants.colorFactors.z = material.pbrData.baseColorFactor[2];
        constants.colorFactors.w = material.pbrData.baseColorFactor[3];

        constants.metalRoughFactors.x = material.pbrData.metallicFactor;
        constants.metalRoughFactors.y = material.pbrData.roughnessFactor;

        materialConstants[dataIndex] = constants;

        auto passType = material.alphaMode == fastgltf::AlphaMode::Blend ? MaterialPass::Transparent : MaterialPass::MainColor;

        VkGLTFMetallic_Roughness::MaterialResources materialResources{};
        materialResources.colorImage = renderer->default_image();
        materialResources.colorSampler = renderer->default_sampler_linear();
        materialResources.metalRoughImage = renderer->default_image();
        materialResources.metalRoughSampler = renderer->default_sampler_linear();

        materialResources.dataBuffer = scene->materialDataBuffer.buffer;
        materialResources.offset = dataIndex * sizeof(VkGLTFMetallic_Roughness::MaterialConstants);

        if (material.pbrData.baseColorTexture.has_value()) {
            auto img = gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            auto sampler = gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

            materialResources.colorImage = images[img];
            materialResources.colorSampler = scene->samplers[sampler];
        }

        auto device = renderer->logical_device();
        newMaterial.data = renderer->metal_rough_material().writeMaterial(device, passType, materialResources, scene->descriptorAllocator);
        dataIndex++;
//        materials.emplace_back(newMaterial);
        materials.push_back(newMaterial);
    }

    renderer->memory_manager()->unmapBuffer(&scene->materialDataBuffer.buffer);

    std::vector<uint16_t> indices;
    std::vector<VkVertex> vertices;

    for (auto &[primitives, weights, name] : gltf.meshes) {
//        auto meshAsset = std::make_shared<MeshAsset>();
        MeshAsset meshAsset{};
        meshAsset.name = name;

        indices.clear();
        vertices.clear();

        for (auto &&primitive: primitives) {
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
                            glm::vec3{1.f},
                            0.f,
                            0.f,
                    };
                });
            }

            // Load vertex normals
            if (auto normals = primitive.findAttribute("NORMAL"); normals != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex],
                                                              [&](auto value, const size_t index) {
                                                                  vertices[initialVerticesSize + index].normal = value;
                                                              });
            }

            // Load tex coords
            if (auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex],
                                                              [&](auto value, const size_t index) {
                                                                  vertices[initialVerticesSize + index].uv_X = value.x;
                                                                  vertices[initialVerticesSize + index].uv_Y = value.y;
                                                              });
            }

            // Load vertex colors
            if (auto color = primitive.findAttribute("COLOR_0"); color != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[color->accessorIndex],
                                                              [&](auto value, const size_t index) {
                                                                  vertices[initialVerticesSize + index].color.x = value.x;
                                                                  vertices[initialVerticesSize + index].color.y = value.y;
                                                                  vertices[initialVerticesSize + index].color.z = value.z;
                                                              });
            }

            if (primitive.materialIndex.has_value()) {
                geoSurface.material = materials[primitive.materialIndex.value()];
            } else {
                geoSurface.material = materials[0];
            }

            glm::vec3 minPos = vertices[initialVerticesSize].pos;
            glm::vec3 maxPos = vertices[initialVerticesSize].pos;
            for (int i = initialVerticesSize; i < vertices.size(); i++) {
                minPos = glm::min(minPos, vertices[i].pos);
                maxPos = glm::max(maxPos, vertices[i].pos);
            }

            geoSurface.bounds.origin = (minPos + maxPos) * 0.5f;
            geoSurface.bounds.extents = (maxPos - minPos) * 0.5f;
            geoSurface.bounds.sphereRadius = glm::length(geoSurface.bounds.extents);

            meshAsset.surfaces.push_back(geoSurface);
        }

        meshAsset.mesh = renderer->CreateMesh(vertices, indices);
        scene->meshes[name.c_str()] = meshAsset;
        meshes.push_back(std::move(meshAsset));
    }

    for (auto &node : gltf.nodes) {
        std::shared_ptr<Node> newNode;

        if (node.meshIndex.has_value()) {
            newNode = std::make_shared<MeshNode>();
            std::dynamic_pointer_cast<MeshNode>(newNode)->meshAsset = meshes[node.meshIndex.value()];
        } else {
            newNode = std::make_shared<Node>();
        }

        nodes.push_back(newNode);
        scene->nodes[node.name.c_str()] = newNode;

        std::visit(fastgltf::visitor {
            [&](fastgltf::math::fmat4x4 &matrix) {
                memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
            },
            [&](fastgltf::TRS &trs) {
                glm::vec3 tl{trs.translation[0], trs.translation[1], trs.translation[2]};
                glm::quat rot{trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]};
                glm::vec3 sc{trs.scale[0], trs.scale[1], trs.scale[2]};

                glm::mat4 transform = glm::translate(glm::mat4{1.f}, tl);
                glm::mat4 rotation = glm::mat4_cast(rot);
                glm::mat4 scale = glm::scale(glm::mat4{1.f}, sc);

                newNode->localTransform = transform * rotation * scale;
            }
        }, node.transform);
    }

    for (int i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node &node = gltf.nodes[i];
        std::shared_ptr<Node> &sceneNode = nodes[i];

        for (auto &c : node.children) {
            nodes[c]->parent = sceneNode;
            sceneNode->children.push_back(nodes[c]);
        }
    }

    for (auto &node : nodes) {
        if (node->parent.lock() == nullptr) {
            scene->rootNodes.push_back(node);
            node->RefreshTransform(glm::mat4{1.f});
        }
    }

    return scene;
}
//#pragma GCC pop_options

void LoadedGLTF::Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) {
    for (auto &node : rootNodes) {
        node->Draw(topMatrix, ctx);
    }
}

void LoadedGLTF::clear() {
    // Buffers and images are automatically cleared by the memory manager
    // TODO: But it may be a good idea to clear them manually if scenes are dynamically loaded and unloaded
    for (auto &sampler : samplers) {
        vkDestroySampler(renderer->logical_device(), sampler, nullptr);
    }

    descriptorAllocator.Destroy(renderer->logical_device());
}
