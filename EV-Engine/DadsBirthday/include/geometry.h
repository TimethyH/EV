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

        // Adds a flat, convex polygon facing the normal (corners in either order).
        void AddPolygon(const std::vector<DirectX::XMFLOAT3>& corners, DirectX::FXMVECTOR normal);

        // Adds a copy of another mesh, moved by a transform without scaling.
        void Append(const MeshData& source, DirectX::FXMMATRIX transform);

        // Adds a box (see BuildBox) with its center at a point.
        void AddBox(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& size);
    };

    // A black and white picture of a shape (true inside), rows top to bottom.
    struct ShapeMask
    {
        uint32_t             width = 0;
        uint32_t             height = 0;
        std::vector<uint8_t> inside;
    };

    // A mask with polygons filled in (even-odd rule). Corners in pixels, y down.
    ShapeMask BuildPolygonMask(uint32_t width, uint32_t height, const std::vector<std::vector<DirectX::XMFLOAT2>>& polygons);

    // The distance (in meters) to the outline of a shape at a grid of points, positive inside.
    struct DistanceField
    {
        uint32_t           width = 0;
        uint32_t           height = 0;
        float              spacing = 0.0f; // Meters between grid points.
        std::vector<float> distance;
    };

    // The distance field of a mask whose pixels are pixelSize meters, sampled every step pixels.
    DistanceField BuildDistanceField(const ShapeMask& mask, float pixelSize, uint32_t step);

    // A slab from z = back to z = front of the part of a field more than inset meters inside the outline, its front
    // facing +z. Grid point (i, j) is at x = -i * spacing, y = -j * spacing, so seen from the front the shape is the
    // right way round (+x is on the left). Without a back (back >= front) only the front is made.
    MeshData ExtrudeShape(const DistanceField& field, float inset, float front, float back);

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
