#pragma once

/**
 *  @file geometry.h
 *
 *  @brief Meshes built in code (terrain, boxes, screen quads) and small helpers for scene nodes and bounds.
 */

#include <DirectXCollision.h>
#include <DirectXMath.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "resources/vertex_types.h"

namespace EV
{
    class CommandList;
    class IndexBuffer;
    class Material;
    class Mesh;
    class SceneNode;
    class VertexBuffer;
}

namespace Birthday
{
    using Vertex = EV::VertexPositionNormalTangentBitangentTexture;

    // A triangle list built on the CPU.
    struct MeshData
    {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;

        // Adds a triangle whose front side faces the way its vertex normals point (the engine culls back faces).
        void AddTriangle(uint32_t a, uint32_t b, uint32_t c);

        // Adds a flat quad. Corners in order: u0v0, u1v0, u0v1, u1v1; the normal is the quad's front.
        void AddQuad(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR uAxis, DirectX::FXMVECTOR vAxis, DirectX::GXMVECTOR normal,
            float uRepeat, float vRepeat);
    };

    // A heightfield over x/z. The texture repeats every 1/uvScale meters.
    MeshData BuildTerrain(float minX, float maxX, float minZ, float maxZ, uint32_t columns, uint32_t rows, float uvScale,
        const std::function<float(float x, float z)>& height);

    // A box centered at the origin. Its texture repeats every 1/uvScale meters (0: once per face).
    MeshData BuildBox(const DirectX::XMFLOAT3& size, float uvScale);

    // A 1x1 quad in the XY plane centered at the origin, facing +z. Seen from +z the image is upright.
    MeshData BuildScreenQuad();

    // Uploads a mesh on a copy (or direct) command list.
    std::shared_ptr<EV::Mesh> CreateMesh(EV::CommandList& commandList, const MeshData& data, const std::shared_ptr<EV::Material>& material);

    // Another mesh with the same vertex and index buffers but its own material.
    std::shared_ptr<EV::Mesh> CloneMesh(EV::Mesh& mesh, const std::shared_ptr<EV::Material>& material);

    // A named node, added to the parent (if any) with the given local transform.
    std::shared_ptr<EV::SceneNode> CreateNode(const std::string& name, const std::shared_ptr<EV::SceneNode>& parent,
        DirectX::FXMMATRIX localTransform = DirectX::XMMatrixIdentity());

    // Copies a node tree (names, transforms and meshes; the meshes are shared) below a new parent.
    std::shared_ptr<EV::SceneNode> CopyNodes(const std::shared_ptr<EV::SceneNode>& source, const std::shared_ptr<EV::SceneNode>& parent);

    // World space bounds of every mesh below a node. False when there are none.
    bool ComputeWorldBounds(const std::shared_ptr<EV::SceneNode>& root, DirectX::BoundingBox& bounds);

    // Bounds of every mesh below a node in the node's own space (its parents and its own transform ignored).
    bool ComputeLocalBounds(const std::shared_ptr<EV::SceneNode>& root, DirectX::BoundingBox& bounds);

    // Bounds of a loaded model as CopyNodes places it: in its parent's space (the root's own transform included).
    bool ComputeModelBounds(const std::shared_ptr<EV::SceneNode>& root, DirectX::BoundingBox& bounds);
}
