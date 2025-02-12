#include "gltf.h"

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <stb_image.h>
#include <ranges>

#include "vk_renderer.h"
#include "gtc/quaternion.hpp"
#include "threading/thread_pool.h"

static std::optional<VulkanImage> loadImage(VkRenderer *renderer, fastgltf::Asset &asset, fastgltf::Image &image, const std::filesystem::path &assetPath) {
    VulkanImage vulkanImage{};

    int width, height, channels;

    std::visit(
        fastgltf::visitor {
            [](auto &) {},
            [&](fastgltf::sources::URI &filePath) {
                assert(filePath.fileByteOffset == 0);
                assert(filePath.uri.isLocalPath());

                const auto path = assetPath / std::filesystem::path(filePath.uri.path());
                if (uint8_t *data = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha)) {
                    const VkExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

                    vulkanImage = renderer->memoryManager->createTexture(data, renderer, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::Array &array) {
                if (uint8_t *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(&array.bytes), static_cast<int>(array.bytes.size_bytes()), &width, &height, &channels, STBI_rgb_alpha)) {
                    const VkExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

                    vulkanImage = renderer->memoryManager->createTexture(data, renderer, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::BufferView &bufferView) {
                assert(bufferView.bufferViewIndex < asset.bufferViews.size());
                const auto &view = asset.bufferViews[bufferView.bufferViewIndex];
                auto &buffer = asset.buffers[view.bufferIndex];

                std::visit(fastgltf::visitor {
                    [](auto &) {},
                    [&](fastgltf::sources::Array &array) {
                        if (uint8_t *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(array.bytes.data()) + view.byteOffset, static_cast<int>(view.byteLength), &width, &height, &channels, STBI_rgb_alpha)) {
                            const VkExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

                            vulkanImage = renderer->memoryManager->createTexture(data, renderer, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                            stbi_image_free(data);
                        }
                    }
                }, buffer.data);
            }
        }, image.data);

    return vulkanImage.image == VK_NULL_HANDLE ? std::nullopt : std::make_optional(vulkanImage);
}

static std::vector<LoadedImage> loadImagesMultithreaded(const fastgltf::Asset &gltf, const std::filesystem::path &assetPath)
{
    const auto &images = gltf.images;
    const auto size = images.size();

    std::vector<LoadedImage> loadedImages(size);

#pragma omp parallel for shared(loadedImages, gltf, images, size, assetPath) default(none) num_threads(std::thread::hardware_concurrency())
    for (uint32_t i = 0; i < size; i++)
    {
        int width, height, channels;
        uint8_t *data;

        const auto dataSource = images[i].data;

        std::visit(
            fastgltf::visitor {
                [](auto &) {},
                [&](const fastgltf::sources::URI &filePath)
                {
                    assert(filePath.fileByteOffset == 0);
                    assert(filePath.uri.isLocalPath());

                    const auto path = assetPath / std::filesystem::path(filePath.uri.path());
                    data = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
                    printf("Loading file: %s\n", filePath.uri.c_str());
                },
                [&](const fastgltf::sources::Array &array) {
                    data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(&array.bytes), static_cast<int>(array.bytes.size_bytes()), &width, &height, &channels, STBI_rgb_alpha);
                    printf("Image loaded from memory\n");
                },
                [&](const fastgltf::sources::BufferView &bufferView) {
                    assert(bufferView.bufferViewIndex < gltf.bufferViews.size());
                    const auto &view = gltf.bufferViews[bufferView.bufferViewIndex];
                    auto &buffer = gltf.buffers[view.bufferIndex];

                    std::visit(fastgltf::visitor {
                            [](auto &) {},
                            [&](fastgltf::sources::Array &array) {
                                data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(array.bytes.data()) + view.byteOffset, static_cast<int>(view.byteLength), &width, &height, &channels, STBI_rgb_alpha);
                            }
                    }, buffer.data);

                    printf("Loading buffer view: %lu\n", bufferView.bufferViewIndex);
                }
        }, dataSource);

        loadedImages[i] = LoadedImage{VkExtent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1}, i, data};
        LoadedImage::totalBytesSize.fetch_add(width * height * 4, std::memory_order_relaxed);
    }

    return loadedImages;
}

inline static VkFilter extractFilter(const fastgltf::Filter filter) {
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

inline static VkSamplerMipmapMode extractMipmapMode(const fastgltf::Filter filter) {
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

std::optional<LoadedGLTF> LoadGLTF(VkRenderer *renderer, bool multithread, const std::filesystem::path &path, const std::filesystem::path &assetPath) {
    LoadedGLTF scene{};

    fastgltf::Parser parser{};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadExternalBuffers;

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(data.error()));
        return std::nullopt;
    }

    auto load = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
    if (load.error() != fastgltf::Error::None) {
        fprintf(stderr, "Failed to load gltf: %llu\n", to_underlying(load.error()));
        return std::nullopt;
    }

    fastgltf::Asset gltf = std::move(load.get());

    static constexpr DescriptorAllocator::PoolSizeRatio sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
    };

    scene.descriptorAllocator.InitPool(renderer->device, gltf.materials.size(), sizes);

    scene.samplers.reserve(gltf.samplers.size());
    for (const auto &[magFilter, minFilter, wrapS, wrapT, name] : gltf.samplers) {
        VkSamplerCreateInfo samplerCreateInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerCreateInfo.magFilter = extractFilter(magFilter.value_or(fastgltf::Filter::Nearest));
        samplerCreateInfo.minFilter = extractFilter(minFilter.value_or(fastgltf::Filter::Nearest));

        samplerCreateInfo.minLod = 0.f;
        samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;

        samplerCreateInfo.mipmapMode = extractMipmapMode(minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        if (vkCreateSampler(renderer->device, &samplerCreateInfo, nullptr, &newSampler) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create sampler: %s\n", name.c_str());
            return std::nullopt;
        }

        scene.samplers.push_back(newSampler);
    }

    std::vector<MeshAsset> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<VulkanImage> images;
    std::vector<GLTFMaterial> materials;

    meshes.reserve(gltf.meshes.size());
    nodes.reserve(gltf.nodes.size());
    materials.reserve(gltf.materials.size());

    auto start = std::chrono::high_resolution_clock::now();
    if (multithread) {
        const auto loadedImages = loadImagesMultithreaded(gltf, assetPath);
        images = renderer->memoryManager->createTexturesMultithreaded(loadedImages, renderer);
        printf("Total bytes loaded: %llu MiB\n", LoadedImage::totalBytesSize.load(std::memory_order_relaxed) >> 20);
    } else {
        images.reserve(gltf.images.size());
        for (auto &image: gltf.images) {
            if (auto loadedImage = loadImage(renderer, gltf, image, assetPath); loadedImage.has_value()) {
                images.push_back(loadedImage.value());
            } else {
                fprintf(stderr, "Failed to load image: %s\n", image.name.c_str());
                images.push_back(renderer->defaultImage);
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    printf("Time to process images: %llu ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    scene.materialDataBuffer = renderer->memoryManager->createManagedBuffer(
            {sizeof(VkGLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
             VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    VkGLTFMetallic_Roughness::MaterialConstants *materialConstants;
    renderer->memoryManager->mapBuffer(scene.materialDataBuffer, reinterpret_cast<void **>(&materialConstants));

    int dataIndex = 0;

    for (auto &material : gltf.materials) {
        GLTFMaterial newMaterial{};

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
        if (material.pbrData.baseColorTexture.has_value()) {
            auto img = gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            materialResources.colorImage = images[img];

            if (gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].samplerIndex.has_value()) {
                auto sampler = gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();
                materialResources.colorSampler = scene.samplers[sampler];
            } else {
                materialResources.colorSampler = renderer->textureSamplerLinear;
            }

        } else {
            materialResources.colorImage = renderer->defaultImage;
            materialResources.colorSampler = renderer->textureSamplerLinear;
        }

        materialResources.metalRoughImage = renderer->defaultImage;
        materialResources.metalRoughSampler = renderer->textureSamplerLinear;

        materialResources.dataBuffer = scene.materialDataBuffer.buffer;
        materialResources.offset = dataIndex * sizeof(VkGLTFMetallic_Roughness::MaterialConstants);

        auto device = renderer->device;
        newMaterial.data = renderer->metalRoughMaterial.writeMaterial(device, passType, materialResources, scene.descriptorAllocator);
        dataIndex++;
        materials.emplace_back(newMaterial);
    }

    renderer->memoryManager->unmapBuffer(scene.materialDataBuffer);

    std::vector<uint32_t> indices;
    std::vector<VkVertex> vertices;

    for (auto &[primitives, weights, name] : gltf.meshes) {
        MeshAsset meshAsset{};
        if (!renderer->meshShader)
            meshAsset.surfaces.reserve(primitives.size());

        indices.clear();
        vertices.clear();

        for (auto &primitive : primitives) {
            GeoSurface geoSurface{
                    static_cast<uint32_t>(indices.size()),
                    static_cast<uint32_t>(gltf.accessors[primitive.indicesAccessor.value()].count)
            };

            size_t initialVerticesSize = vertices.size();

            // Load indices
            {
                auto &indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);

                fastgltf::iterateAccessor<uint32_t>(gltf, indexAccessor, [&](auto value) {
                    indices.push_back(static_cast<uint32_t>(initialVerticesSize + value));
                });
            }

            // Load vertex positions
            {
                auto &posAccessor = gltf.accessors[primitive.findAttribute("POSITION")->accessorIndex];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](auto value, const size_t index) {
                    vertices[initialVerticesSize + index] = {
                            {value, 1.0f},
                            {1, 0, 0},
                            {1.f, 1.f, 1.f, 1.f},
                            {0.f, 1.f}
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
                                                                  vertices[initialVerticesSize + index].uv = value;
                                                              });
            }

            // Load vertex colors
            if (auto color = primitive.findAttribute("COLOR_0"); color != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[color->accessorIndex],
                                                              [&](auto value, const size_t index) {
                                                                  vertices[initialVerticesSize + index].color = value;
                                                              });
            }

            if (renderer->meshShader)
                continue;

            geoSurface.material = materials[primitive.materialIndex.value() * primitive.materialIndex.has_value()];

            glm::vec4 minPos = vertices[initialVerticesSize].pos;
            glm::vec4 maxPos = vertices[initialVerticesSize].pos;
            for (int i = static_cast<int>(initialVerticesSize); i < vertices.size(); i++) {
                minPos = min(minPos, vertices[i].pos);
                maxPos = max(maxPos, vertices[i].pos);
            }

            geoSurface.bounds.origin = (minPos + maxPos) * 0.5f;
            geoSurface.bounds.extents = (maxPos - minPos) * 0.5f;
            geoSurface.bounds.sphereRadius = length(geoSurface.bounds.extents);

            meshAsset.surfaces.push_back(geoSurface);
        }

        if (renderer->meshShader) {
            renderer->CreateFromMeshlets(vertices, indices);
        } else {
            meshAsset.mesh = renderer->CreateMesh(vertices, indices);
            meshes.push_back(std::move(meshAsset));
        }
    }

    constexpr auto identity = glm::mat4{1.f};
    for (auto &node : gltf.nodes) {
        auto newNode = std::make_shared<Node>();

        if (!renderer->meshShader)
        {
            newNode->type = static_cast<NodeType>(node.meshIndex.has_value());
            newNode->meshAsset = static_cast<bool>(newNode->type) ? meshes[node.meshIndex.value()] : MeshAsset{};
        }

        std::visit(fastgltf::visitor {
            [&](fastgltf::math::fmat4x4 &matrix) {
                memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
            },
            [&](fastgltf::TRS &trs) {
                const glm::vec3 tl = *reinterpret_cast<glm::vec3 *>(&trs.translation);
                const glm::quat rot{trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]};
                const glm::vec3 sc = *reinterpret_cast<glm::vec3 *>(&trs.scale);

                const glm::mat4 transform = translate(identity, tl);
                const glm::mat4 rotation = mat4_cast(rot);
                const glm::mat4 scale = glm::scale(identity, sc);

                newNode->localTransform = transform * rotation * scale;
            }
        }, node.transform);

        nodes.emplace_back(newNode);
    }

    for (int i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node &node = gltf.nodes[i];
        std::shared_ptr<Node> &sceneNode = nodes[i];

        sceneNode->children.reserve(node.children.size());
        for (auto &c : node.children) {
            nodes[c]->parent = sceneNode;
            sceneNode->children.push_back(nodes[c]);
        }
    }

    for (auto &node : nodes) {
        if (!node->parent.lock()) {
            scene.rootNodes.push_back(node);
            node->RefreshTransform(identity);
        }
    }

    return scene;
}

void LoadedGLTF::Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) {
    for (auto &node : rootNodes) {
        node->Draw(topMatrix, ctx);
    }
}

void LoadedGLTF::Clear(const VkRenderer *renderer) {
    // Buffers and images are automatically cleared by the memory manager
    // TODO: But it may be a good idea to clear them manually if scenes are dynamically loaded and unloaded
    for (auto &sampler : samplers) {
        vkDestroySampler(renderer->device, sampler, nullptr);
    }

    descriptorAllocator.Destroy(renderer->device);
}
