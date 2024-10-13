//
// Created by Ammar on 12/10/2024.
//

#include "render_object.h"

void MeshNode::Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) {
    const glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (const auto &[startIndex, indexCount, material] : mesh->surfaces) {
        VkRenderObject renderObject;
        renderObject.indexCount = indexCount;
        renderObject.firstIndex = startIndex;
        renderObject.indexBuffer = mesh->mesh.indexBuffer;
        renderObject.materialInstance = &material->data;
        renderObject.transform = nodeMatrix;
        renderObject.vertexBufferAddress = mesh->mesh.vertexBufferDeviceAddress;

        ctx.opaqueSurfaces.emplace_back(renderObject);
    }

    Node::Draw(topMatrix, ctx);
}

