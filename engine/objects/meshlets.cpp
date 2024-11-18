#include "meshlets.h"

#include <meshoptimizer.h>
#include <unordered_map>

inline float reduce_f32_m256(const __m256 v) {
    __m128 v128 = _mm256_castps256_ps128(v);
    v128 = _mm_add_ps(v128, _mm_movehl_ps(v128, v128));
    v128 = _mm_add_ss(v128, _mm_shuffle_ps(v128, v128, 1));
    return _mm_cvtss_f32(v128);
}

// TODO: Optimize this
std::vector<Meshlet> GenerateMeshlets(const std::vector<VkVertex> &vertices, const std::vector<uint32_t> &indices) {
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletPrimitives;

    const auto maxMeshlets = meshopt_buildMeshletsBound(indices.size(), MAX_MESHLET_VERTICES, MAX_MESHLET_PRIMITIVES);

    meshlets.resize(maxMeshlets);
    meshletVertices.resize(maxMeshlets * MAX_MESHLET_VERTICES);
    meshletPrimitives.resize(maxMeshlets * MAX_MESHLET_PRIMITIVES);

    std::vector<float> vertexPositionData;
    vertexPositionData.reserve(vertices.size() * 3);

    for (const auto &[pos, normal, color, uv] : vertices) {
        vertexPositionData.push_back(pos.x);
        vertexPositionData.push_back(pos.y);
        vertexPositionData.push_back(pos.z);
    }

    constexpr float coneWeight = 0.0f;
    auto meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletPrimitives.data(),
                                              indices.data(), indices.size(), vertexPositionData.data(),
                                              vertices.size(), sizeof(glm::vec3), MAX_MESHLET_VERTICES,
                                              MAX_MESHLET_PRIMITIVES, coneWeight);

    const auto &[vertex_offset, triangle_offset, vertex_count, triangle_count] = meshlets[meshletCount - 1];
    meshletVertices.resize(vertex_offset + vertex_count);
    meshletPrimitives.resize(triangle_offset + (triangle_count * 3 + 3 & ~3));
    meshlets.resize(meshletCount);

    std::vector<uint32_t> meshletPrimitivesU32;
    for (auto &[vertex_offset, triangle_offset, vertex_count, triangle_count] : meshlets) {
        const auto primitiveOffset = static_cast<uint32_t>(meshletPrimitivesU32.size());

        for (uint32_t i = 0; i < triangle_count; i++) {
            const auto i1 = i * 3 + 0 + triangle_offset;
            const auto i2 = i * 3 + 1 + triangle_offset;
            const auto i3 = i * 3 + 2 + triangle_offset;

            const auto vIdx0 = meshletVertices[i1];
            const auto vIdx1 = meshletVertices[i2];
            const auto vIdx2 = meshletVertices[i3];

            auto packed = vIdx0 & 0xFF | (vIdx1 & 0xFF) << 8 | (vIdx2 & 0xFF) << 16;

            meshletPrimitivesU32.push_back(packed);
        }

        triangle_offset = primitiveOffset;
    }
    // std::vector<Meshlet> meshlets;
    //
    // std::unordered_map<uint32_t, uint32_t> vertexToMeshlet;
    // std::vector<uint32_t> meshletIndices;
    // std::vector<VkVertex> meshletVertices;
    //
    // uint32_t currentVertex = 0;
    // uint32_t currentIndex = 0;
    //
    // while (currentIndex < indices.size()) {
    //     Meshlet meshlet{
    //         currentVertex,
    //         0,
    //         currentIndex,
    //         0
    //     };
    //
    //     while (currentIndex + 3 <= indices.size() && meshletVertices.size() < MAX_MESHLET_VERTICES && meshletIndices.size() < MAX_MESHLET_PRIMITIVES) {
    //         uint32_t i0 = indices[currentIndex];
    //         uint32_t i1 = indices[currentIndex + 1];
    //         uint32_t i2 = indices[currentIndex + 2];
    //
    //         if (!vertexToMeshlet.contains(i0))
    //             vertexToMeshlet[i0] = meshletVertices.size();
    //
    //         if (!vertexToMeshlet.contains(i1))
    //             vertexToMeshlet[i1] = meshletVertices.size();
    //
    //         if (!vertexToMeshlet.contains(i2))
    //             vertexToMeshlet[i2] = meshletVertices.size();
    //
    //         // vertexToMeshlet[i0] = !vertexToMeshlet.contains(i0) ? meshletVertices.size() : vertexToMeshlet[i0];
    //         // vertexToMeshlet[i1] = !vertexToMeshlet.contains(i1) ? meshletVertices.size() : vertexToMeshlet[i1];
    //         // vertexToMeshlet[i2] = !vertexToMeshlet.contains(i2) ? meshletVertices.size() : vertexToMeshlet[i2];
    //
    //         if (meshletVertices.size() + vertexToMeshlet.size() <= MAX_MESHLET_VERTICES) {
    //             if (!vertexToMeshlet.contains(i0)) {
    //                 meshletVertices.push_back(vertices[i0]);
    //             }
    //
    //             if (!vertexToMeshlet.contains(i1)) {
    //                 meshletVertices.push_back(vertices[i1]);
    //             }
    //
    //             if (!vertexToMeshlet.contains(i2)) {
    //                 meshletVertices.push_back(vertices[i2]);
    //             }
    //
    //             meshletIndices.push_back(vertexToMeshlet[i0]);
    //             meshletIndices.push_back(vertexToMeshlet[i1]);
    //             meshletIndices.push_back(vertexToMeshlet[i2]);
    //
    //             currentIndex += 3;
    //         } else {
    //             break;
    //         }
    //     }
    //
    //     currentVertex += meshletVertices.size();
    //     meshlet.vertexCount = meshletVertices.size() - meshlet.vertexOffset;
    //     meshlet.indexCount = meshletIndices.size() - meshlet.indexOffset;
    //
    //     glm::vec3 center{0.f};
    //     uint32_t i = 0;
    //     for (; i < meshletVertices.size(); i += 8) {
    //         const auto v1 = meshletVertices[i].pos;
    //         const auto v2 = meshletVertices[i + 1].pos;
    //         const auto v3 = meshletVertices[i + 2].pos;
    //         const auto v4 = meshletVertices[i + 3].pos;
    //         const auto v5 = meshletVertices[i + 4].pos;
    //         const auto v6 = meshletVertices[i + 5].pos;
    //         const auto v7 = meshletVertices[i + 6].pos;
    //         const auto v8 = meshletVertices[i + 7].pos;
    //
    //         __m256 v1x = _mm256_set_ps(v1.x, v2.x, v3.x, v4.x, v5.x, v6.x, v7.x, v8.x);
    //         __m256 v1y = _mm256_set_ps(v1.y, v2.y, v3.y, v4.y, v5.y, v6.y, v7.y, v8.y);
    //         __m256 v1z = _mm256_set_ps(v1.z, v2.z, v3.z, v4.z, v5.z, v6.z, v7.z, v8.z);
    //
    //         center.x += reduce_f32_m256(v1x);
    //         center.y += reduce_f32_m256(v1y);
    //         center.z += reduce_f32_m256(v1z);
    //     }
    //
    //     for (i -= 8; i < meshletVertices.size(); i++) {
    //         center += meshletVertices[i].pos;
    //     }
    //     // for (const auto &vertex : meshletVertices) {
    //     //     center += vertex.pos;
    //     // }
    //     center /= static_cast<float>(meshletVertices.size());
    //
    //     float maxRadius = 0.f;
    //     for (const auto &vertex : meshletVertices) {
    //         auto resultVector = center - vertex.pos;
    //         // maxRadius = std::max(maxRadius, distance(center, vertex.pos));
    //         auto distanceSquared = dot(resultVector, resultVector);
    //         maxRadius = distanceSquared < 1.f ? std::min(maxRadius, distanceSquared) : std::max(maxRadius, distanceSquared);
    //         // if (distanceSquared < 1.f) {
    //         //     maxRadius = std::min(maxRadius, distanceSquared);
    //         // } else {
    //         //     maxRadius = std::max(maxRadius, distanceSquared);
    //         // }
    //     }
    //
    //     meshlet.boundingSphere = glm::vec4{center, maxRadius};
    //
    //     meshlets.emplace_back(meshlet);
    //
    //     vertexToMeshlet.clear();
    //     meshletVertices.clear();
    //     meshletIndices.clear();
    // }
    //
    // return meshlets;
}