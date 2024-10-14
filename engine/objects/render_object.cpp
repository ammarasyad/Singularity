//
// Created by Ammar on 12/10/2024.
//

#include "render_object.h"

void MeshNode::Draw(const glm::mat4 &topMatrix, VkDrawContext &ctx) {
    const glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto &[startIndex, indexCount, bounds, material] : meshAsset.surfaces) {
        VkRenderObject renderObject;
        renderObject.indexCount = indexCount;
        renderObject.firstIndex = startIndex;
        renderObject.indexBuffer = meshAsset.mesh.indexBuffer;
        renderObject.materialInstance = &material.data;
        renderObject.bounds = bounds;
        renderObject.transform = nodeMatrix;
        renderObject.vertexBufferAddress = meshAsset.mesh.vertexBufferDeviceAddress;

//        ctx.opaqueSurfaces.emplace_back(renderObject);
        if (material.data.pass == MaterialPass::MainColor) {
            ctx.opaqueSurfaces.emplace_back(renderObject);
        } else if (material.data.pass == MaterialPass::Transparent) {
            ctx.transparentSurfaces.emplace_back(renderObject);
        }
    }

    Node::Draw(topMatrix, ctx);
}

