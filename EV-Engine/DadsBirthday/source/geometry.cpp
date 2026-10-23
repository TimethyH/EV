#include "core/EV.h"

#include "geometry.h"

using namespace DirectX;
using namespace EV;

namespace Birthday
{
    void MeshData::AddTriangle(uint32_t a, uint32_t b, uint32_t c)
    {
        // The engine draws clockwise triangles (seen from the front); in its left-handed space that means
        // cross(b - a, c - a) points to the front. Flip the triangle when it points away from the vertex normals.
        const XMVECTOR pa = XMLoadFloat3(&vertices[a].position);
        const XMVECTOR pb = XMLoadFloat3(&vertices[b].position);
        const XMVECTOR pc = XMLoadFloat3(&vertices[c].position);
        const XMVECTOR faceNormal = XMVector3Cross(pb - pa, pc - pa);
        const XMVECTOR normal = XMLoadFloat3(&vertices[a].normal) + XMLoadFloat3(&vertices[b].normal) + XMLoadFloat3(&vertices[c].normal);
        if (XMVectorGetX(XMVector3Dot(faceNormal, normal)) < 0.0f)
        {
            std::swap(b, c);
        }
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }

    void MeshData::AddQuad(FXMVECTOR origin, FXMVECTOR uAxis, FXMVECTOR vAxis, GXMVECTOR normal, float uRepeat, float vRepeat)
    {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        const XMVECTOR tangent = XMVector3Normalize(uAxis);
        const XMVECTOR bitangent = XMVector3Normalize(vAxis);
        const XMVECTOR n = XMVector3Normalize(normal);
        for (int corner = 0; corner < 4; ++corner)
        {
            const float u = (corner & 1) ? 1.0f : 0.0f;
            const float v = (corner & 2) ? 1.0f : 0.0f;
            const XMVECTOR position = origin + uAxis * u + vAxis * v;
            vertices.emplace_back(position, n, XMVectorSet(u * uRepeat, v * vRepeat, 0.0f, 0.0f), tangent, bitangent);
        }
        AddTriangle(first + 0, first + 1, first + 2);
        AddTriangle(first + 1, first + 3, first + 2);
    }

    MeshData BuildTerrain(float minX, float maxX, float minZ, float maxZ, uint32_t columns, uint32_t rows, float uvScale,
        const std::function<float(float x, float z)>& height)
    {
        MeshData data;
        data.vertices.reserve(static_cast<size_t>(columns + 1) * (rows + 1));

        const float epsilon = 0.05f;
        for (uint32_t row = 0; row <= rows; ++row)
        {
            const float z = minZ + (maxZ - minZ) * row / rows;
            for (uint32_t column = 0; column <= columns; ++column)
            {
                const float x = minX + (maxX - minX) * column / columns;
                const float y = height(x, z);
                const float dx = (height(x + epsilon, z) - height(x - epsilon, z)) / (2.0f * epsilon);
                const float dz = (height(x, z + epsilon) - height(x, z - epsilon)) / (2.0f * epsilon);

                // u follows +x and v follows -z, so the tangent is dP/dx and the bitangent -dP/dz (DirectX normal maps).
                const XMVECTOR normal = XMVector3Normalize(XMVectorSet(-dx, 1.0f, -dz, 0.0f));
                const XMVECTOR tangent = XMVector3Normalize(XMVectorSet(1.0f, dx, 0.0f, 0.0f));
                const XMVECTOR bitangent = XMVector3Normalize(XMVectorSet(0.0f, -dz, -1.0f, 0.0f));
                data.vertices.emplace_back(XMVectorSet(x, y, z, 1.0f), normal, XMVectorSet(x * uvScale, -z * uvScale, 0.0f, 0.0f),
                    tangent, bitangent);
            }
        }

        const uint32_t stride = columns + 1;
        for (uint32_t row = 0; row < rows; ++row)
        {
            for (uint32_t column = 0; column < columns; ++column)
            {
                const uint32_t i00 = row * stride + column;
                const uint32_t i10 = i00 + 1;
                const uint32_t i01 = i00 + stride;
                const uint32_t i11 = i01 + 1;
                data.AddTriangle(i00, i10, i01);
                data.AddTriangle(i10, i11, i01);
            }
        }
        return data;
    }

    MeshData BuildBox(const XMFLOAT3& size, float uvScale)
    {
        const float hx = size.x * 0.5f;
        const float hy = size.y * 0.5f;
        const float hz = size.z * 0.5f;

        // Per face: the corner that is top left seen from outside, the u (right) and v (down) edges and the normal.
        struct Face
        {
            XMFLOAT3 origin, u, v, normal;
        };
        const Face faces[] = {
            { { +hx, +hy, +hz }, { -2 * hx, 0, 0 }, { 0, -2 * hy, 0 }, { 0, 0, 1 } },
            { { -hx, +hy, -hz }, { +2 * hx, 0, 0 }, { 0, -2 * hy, 0 }, { 0, 0, -1 } },
            { { +hx, +hy, -hz }, { 0, 0, +2 * hz }, { 0, -2 * hy, 0 }, { 1, 0, 0 } },
            { { -hx, +hy, +hz }, { 0, 0, -2 * hz }, { 0, -2 * hy, 0 }, { -1, 0, 0 } },
            { { -hx, +hy, +hz }, { +2 * hx, 0, 0 }, { 0, 0, -2 * hz }, { 0, 1, 0 } },
            { { -hx, -hy, -hz }, { +2 * hx, 0, 0 }, { 0, 0, +2 * hz }, { 0, -1, 0 } },
        };

        MeshData data;
        for (const Face& face : faces)
        {
            const XMVECTOR u = XMLoadFloat3(&face.u);
            const XMVECTOR v = XMLoadFloat3(&face.v);
            const float uRepeat = uvScale > 0.0f ? XMVectorGetX(XMVector3Length(u)) * uvScale : 1.0f;
            const float vRepeat = uvScale > 0.0f ? XMVectorGetX(XMVector3Length(v)) * uvScale : 1.0f;
            data.AddQuad(XMLoadFloat3(&face.origin), u, v, XMLoadFloat3(&face.normal), uRepeat, vRepeat);
        }
        return data;
    }

    MeshData BuildScreenQuad()
    {
        // Seen from +z (looking towards -z), +x is on the left.
        MeshData data;
        data.AddQuad(XMVectorSet(0.5f, 0.5f, 0.0f, 1.0f), XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f),
            XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), 1.0f, 1.0f);
        return data;
    }

    std::shared_ptr<Mesh> CreateMesh(CommandList& commandList, const MeshData& data, const std::shared_ptr<Material>& material)
    {
        auto mesh = std::make_shared<Mesh>();
        mesh->SetVertexBuffer(0, commandList.CopyVertexBuffer(data.vertices));
        mesh->SetIndexBuffer(commandList.CopyIndexBuffer(data.indices));
        mesh->SetMaterial(material);

        BoundingBox bounds;
        BoundingBox::CreateFromPoints(bounds, data.vertices.size(), &data.vertices[0].position, sizeof(Vertex));
        mesh->SetAABB(bounds);
        return mesh;
    }

    std::shared_ptr<Mesh> CloneMesh(Mesh& mesh, const std::shared_ptr<Material>& material)
    {
        auto clone = std::make_shared<Mesh>();
        for (const auto& [slot, vertexBuffer] : mesh.GetVertexBuffers())
        {
            clone->SetVertexBuffer(slot, vertexBuffer);
        }
        clone->SetIndexBuffer(mesh.GetIndexBuffer());
        clone->SetPrimitiveTopology(mesh.GetPrimitiveTopology());
        clone->SetAABB(mesh.GetAABB());
        clone->SetMaterial(material);
        return clone;
    }

    std::shared_ptr<SceneNode> CreateNode(const std::string& name, const std::shared_ptr<SceneNode>& parent, FXMMATRIX localTransform)
    {
        auto node = std::make_shared<SceneNode>();
        node->SetName(name);
        if (parent)
        {
            // AddChild keeps the world transform, so the local transform is set afterwards.
            parent->AddChild(node);
        }
        node->SetLocalTransform(localTransform);
        return node;
    }

    std::shared_ptr<SceneNode> CopyNodes(const std::shared_ptr<SceneNode>& source, const std::shared_ptr<SceneNode>& parent)
    {
        auto node = CreateNode(source->GetName(), parent, source->GetLocalTransform());
        for (const auto& mesh : source->GetMeshes())
        {
            node->AddMesh(mesh);
        }
        for (const auto& child : source->GetChildren())
        {
            CopyNodes(child, node);
        }
        return node;
    }

    namespace
    {
        void MergeBounds(const std::shared_ptr<SceneNode>& node, FXMMATRIX transform, BoundingBox& bounds, bool& any)
        {
            for (const auto& mesh : node->GetMeshes())
            {
                if (!mesh)
                {
                    continue;
                }
                BoundingBox meshBounds;
                mesh->GetAABB().Transform(meshBounds, transform);
                if (any)
                {
                    BoundingBox::CreateMerged(bounds, bounds, meshBounds);
                }
                else
                {
                    bounds = meshBounds;
                    any = true;
                }
            }
            for (const auto& child : node->GetChildren())
            {
                MergeBounds(child, child->GetLocalTransform() * transform, bounds, any);
            }
        }
    }

    bool ComputeWorldBounds(const std::shared_ptr<SceneNode>& root, BoundingBox& bounds)
    {
        bool any = false;
        if (root)
        {
            MergeBounds(root, root->GetWorldTransform(), bounds, any);
        }
        return any;
    }

    bool ComputeLocalBounds(const std::shared_ptr<SceneNode>& root, BoundingBox& bounds)
    {
        bool any = false;
        if (root)
        {
            MergeBounds(root, XMMatrixIdentity(), bounds, any);
        }
        return any;
    }

    bool ComputeModelBounds(const std::shared_ptr<SceneNode>& root, BoundingBox& bounds)
    {
        bool any = false;
        if (root)
        {
            MergeBounds(root, root->GetLocalTransform(), bounds, any);
        }
        return any;
    }
}
