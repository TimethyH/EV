#include "core/EV.h"

#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

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

    void MeshData::AddPolygon(const std::vector<XMFLOAT3>& corners, FXMVECTOR normal)
    {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        const XMVECTOR n = XMVector3Normalize(normal);
        // Any direction in the plane works as the tangent: these meshes have no normal maps.
        const XMVECTOR up = std::abs(XMVectorGetY(n)) < 0.9f ? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        const XMVECTOR tangent = XMVector3Normalize(XMVector3Cross(up, n));
        const XMVECTOR bitangent = XMVector3Cross(n, tangent);
        for (const XMFLOAT3& corner : corners)
        {
            const XMVECTOR position = XMLoadFloat3(&corner);
            vertices.emplace_back(position, n, XMVectorSet(XMVectorGetX(XMVector3Dot(position, tangent)),
                XMVectorGetX(XMVector3Dot(position, bitangent)), 0.0f, 0.0f), tangent, bitangent);
        }
        for (uint32_t i = 2; i < corners.size(); ++i)
        {
            AddTriangle(first, first + i - 1, first + i);
        }
    }

    void MeshData::Append(const MeshData& source, FXMMATRIX transform)
    {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        for (const Vertex& vertex : source.vertices)
        {
            Vertex moved = vertex;
            XMStoreFloat3(&moved.position, XMVector3TransformCoord(XMLoadFloat3(&vertex.position), transform));
            XMStoreFloat3(&moved.normal, XMVector3TransformNormal(XMLoadFloat3(&vertex.normal), transform));
            XMStoreFloat3(&moved.tangent, XMVector3TransformNormal(XMLoadFloat3(&vertex.tangent), transform));
            XMStoreFloat3(&moved.bitangent, XMVector3TransformNormal(XMLoadFloat3(&vertex.bitangent), transform));
            vertices.push_back(moved);
        }
        for (uint32_t index : source.indices)
        {
            indices.push_back(first + index);
        }
    }

    void MeshData::AddBox(const XMFLOAT3& center, const XMFLOAT3& size)
    {
        Append(BuildBox(size, 0.0f), XMMatrixTranslation(center.x, center.y, center.z));
    }

    ShapeMask BuildPolygonMask(uint32_t width, uint32_t height, const std::vector<std::vector<XMFLOAT2>>& polygons)
    {
        ShapeMask mask;
        mask.width = width;
        mask.height = height;
        mask.inside.assign(static_cast<size_t>(width) * height, 0);
        for (uint32_t y = 0; y < height; ++y)
        {
            const float py = y + 0.5f;
            for (const auto& polygon : polygons)
            {
                // Where the polygon's edges cross this row; between pairs of crossings is inside.
                std::vector<float> crossings;
                for (size_t i = 0; i < polygon.size(); ++i)
                {
                    const XMFLOAT2& a = polygon[i];
                    const XMFLOAT2& b = polygon[(i + 1) % polygon.size()];
                    if ((a.y <= py) != (b.y <= py))
                    {
                        crossings.push_back(a.x + (py - a.y) / (b.y - a.y) * (b.x - a.x));
                    }
                }
                std::sort(crossings.begin(), crossings.end());
                for (size_t i = 0; i + 1 < crossings.size(); i += 2)
                {
                    const int begin = std::max(0, static_cast<int>(std::ceil(crossings[i] - 0.5f)));
                    const int end = std::min(static_cast<int>(width) - 1, static_cast<int>(std::floor(crossings[i + 1] - 0.5f)));
                    for (int x = begin; x <= end; ++x)
                    {
                        mask.inside[static_cast<size_t>(y) * width + x] ^= 1;
                    }
                }
            }
        }
        return mask;
    }

    namespace
    {
        // Squared distance to the nearest feature along one line (Felzenszwalb and Huttenlocher). f: 0 at features,
        // a large number elsewhere.
        void DistanceTransform1D(const std::vector<float>& f, std::vector<float>& d, std::vector<int>& v, std::vector<float>& z)
        {
            const int n = static_cast<int>(f.size());
            int k = 0;
            v[0] = 0;
            z[0] = -1e20f;
            z[1] = 1e20f;
            for (int q = 1; q < n; ++q)
            {
                float s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * (q - v[k]));
                while (s <= z[k])
                {
                    --k;
                    s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * (q - v[k]));
                }
                ++k;
                v[k] = q;
                z[k] = s;
                z[k + 1] = 1e20f;
            }
            k = 0;
            for (int q = 0; q < n; ++q)
            {
                while (z[k + 1] < q)
                {
                    ++k;
                }
                d[q] = (q - v[k]) * (q - v[k]) + f[v[k]];
            }
        }

        // Squared distance (pixels) from every pixel to the nearest pixel whose mask value is the given one.
        std::vector<float> SquaredDistanceTo(const ShapeMask& mask, uint8_t feature)
        {
            const uint32_t w = mask.width;
            const uint32_t h = mask.height;
            std::vector<float> grid(static_cast<size_t>(w) * h);
            for (size_t i = 0; i < grid.size(); ++i)
            {
                grid[i] = mask.inside[i] == feature ? 0.0f : 1e20f;
            }
            const uint32_t longest = std::max(w, h);
            std::vector<float> f(longest), d(longest), z(longest + 1);
            std::vector<int> v(longest);
            f.resize(h); d.resize(h);
            for (uint32_t x = 0; x < w; ++x)
            {
                for (uint32_t y = 0; y < h; ++y) f[y] = grid[y * w + x];
                DistanceTransform1D(f, d, v, z);
                for (uint32_t y = 0; y < h; ++y) grid[y * w + x] = d[y];
            }
            f.resize(w); d.resize(w);
            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x) f[x] = grid[y * w + x];
                DistanceTransform1D(f, d, v, z);
                for (uint32_t x = 0; x < w; ++x) grid[y * w + x] = d[x];
            }
            return grid;
        }
    }

    DistanceField BuildDistanceField(const ShapeMask& mask, float pixelSize, uint32_t step)
    {
        const std::vector<float> toOutside = SquaredDistanceTo(mask, 0);
        const std::vector<float> toInside = SquaredDistanceTo(mask, 1);

        DistanceField field;
        field.width = (mask.width - 1) / step + 1;
        field.height = (mask.height - 1) / step + 1;
        field.spacing = pixelSize * step;
        field.distance.resize(static_cast<size_t>(field.width) * field.height);
        for (uint32_t j = 0; j < field.height; ++j)
        {
            for (uint32_t i = 0; i < field.width; ++i)
            {
                // The outline runs between pixel centers, half a pixel from the nearest pixel on the other side.
                const size_t pixel = static_cast<size_t>(j * step) * mask.width + i * step;
                const float pixels = mask.inside[pixel] ? std::sqrt(toOutside[pixel]) - 0.5f : 0.5f - std::sqrt(toInside[pixel]);
                field.distance[static_cast<size_t>(j) * field.width + i] = pixels * pixelSize;
            }
        }
        return field;
    }

    MeshData ExtrudeShape(const DistanceField& field, float inset, float front, float back)
    {
        MeshData data;
        const bool solid = back < front;
        const float spacing = field.spacing;
        const uint32_t w = field.width;
        const uint32_t h = field.height;
        auto value = [&](int i, int j)
        {
            i = std::clamp(i, 0, static_cast<int>(w) - 1);
            j = std::clamp(j, 0, static_cast<int>(h) - 1);
            return field.distance[static_cast<size_t>(j) * w + i] - inset;
        };
        // Outwards (where the distance gets smaller) in world space: grid +i is world -x and +j is world -y.
        auto outward = [&](int i, int j)
        {
            return XMVectorSet(value(i + 1, j) - value(i - 1, j), value(i, j + 1) - value(i, j - 1), 0.0f, 0.0f);
        };

        struct Point
        {
            float    i, j;
            bool     onOutline;
            XMVECTOR normal; // On the outline only.
        };
        auto addCaps = [&](const std::vector<Point>& polygon)
        {
            std::vector<XMFLOAT3> corners;
            for (const Point& point : polygon)
            {
                corners.emplace_back(-point.i * spacing, -point.j * spacing, front);
            }
            data.AddPolygon(corners, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
            if (solid)
            {
                for (XMFLOAT3& corner : corners)
                {
                    corner.z = back;
                }
                data.AddPolygon(corners, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
            }
        };
        auto addSide = [&](const Point& a, const Point& b)
        {
            const float sideBack = solid ? back : front - 0.01f;
            const uint32_t first = static_cast<uint32_t>(data.vertices.size());
            const XMVECTOR tangent = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
            for (const Point* point : { &a, &b })
            {
                const XMVECTOR normal = XMVector3Normalize(point->normal);
                const XMVECTOR bitangent = XMVector3Cross(normal, tangent);
                for (float z : { front, sideBack })
                {
                    data.vertices.emplace_back(XMVectorSet(-point->i * spacing, -point->j * spacing, z, 1.0f), normal,
                        XMVectorSet(point->i * spacing, z, 0.0f, 0.0f), tangent, bitangent);
                }
            }
            data.AddTriangle(first, first + 1, first + 2);
            data.AddTriangle(first + 1, first + 3, first + 2);
        };

        for (uint32_t j = 0; j + 1 < h; ++j)
        {
            // Cells that are completely inside are joined into one quad per run.
            int runStart = -1;
            for (uint32_t i = 0; i + 1 <= w; ++i)
            {
                const bool full = i + 1 < w && value(i, j) > 0.0f && value(i + 1, j) > 0.0f && value(i + 1, j + 1) > 0.0f && value(i, j + 1) > 0.0f;
                if (full)
                {
                    if (runStart < 0)
                    {
                        runStart = static_cast<int>(i);
                    }
                    continue;
                }
                if (runStart >= 0)
                {
                    const float i0 = static_cast<float>(runStart);
                    const float i1 = static_cast<float>(i);
                    const float j0 = static_cast<float>(j);
                    addCaps({ { i0, j0, false, {} }, { i1, j0, false, {} }, { i1, j0 + 1, false, {} }, { i0, j0 + 1, false, {} } });
                    runStart = -1;
                }
                if (i + 1 >= w)
                {
                    continue;
                }

                // A cell on the outline: walk its corners, adding the inside ones and where the outline crosses an edge.
                const int ci[4] = { static_cast<int>(i), static_cast<int>(i) + 1, static_cast<int>(i) + 1, static_cast<int>(i) };
                const int cj[4] = { static_cast<int>(j), static_cast<int>(j), static_cast<int>(j) + 1, static_cast<int>(j) + 1 };
                std::vector<Point> polygon;
                for (int k = 0; k < 4; ++k)
                {
                    const int n = (k + 1) % 4;
                    const float a = value(ci[k], cj[k]);
                    const float b = value(ci[n], cj[n]);
                    if (a > 0.0f)
                    {
                        polygon.push_back({ static_cast<float>(ci[k]), static_cast<float>(cj[k]), false, {} });
                    }
                    if ((a > 0.0f) != (b > 0.0f))
                    {
                        const float t = a / (a - b);
                        const XMVECTOR normal = XMVectorLerp(outward(ci[k], cj[k]), outward(ci[n], cj[n]), t);
                        polygon.push_back({ ci[k] + (ci[n] - ci[k]) * t, cj[k] + (cj[n] - cj[k]) * t, true, normal });
                    }
                }
                if (polygon.size() < 3)
                {
                    continue;
                }
                addCaps(polygon);
                // Two outline points in a row are a piece of the outline: a wall from the front to the back.
                for (size_t k = 0; k < polygon.size(); ++k)
                {
                    const Point& a = polygon[k];
                    const Point& b = polygon[(k + 1) % polygon.size()];
                    if (a.onOutline && b.onOutline)
                    {
                        addSide(a, b);
                    }
                }
            }
        }
        return data;
    }

    void WriteObj(const std::filesystem::path& path, const std::vector<ObjPart>& parts, FXMMATRIX transform)
    {
        std::filesystem::path materialPath = path;
        materialPath.replace_extension(".mtl");
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream obj(path);
        std::ofstream mtl(materialPath);
        if (!obj || !mtl)
        {
            throw std::runtime_error("Could not write " + path.string());
        }
        obj << "# Made by the DadsBirthday game (--export-models).\nmtllib " << materialPath.filename().string() << "\n";
        mtl << "# Colors are linear, as the EV engine uses them. Pr/Pm: PBR roughness and metalness.\n";

        // Positions and normals are written once each (to the millimeter); faces refer to them by number, from 1.
        std::unordered_map<std::string, uint32_t> positions, normals;
        auto index = [&](std::unordered_map<std::string, uint32_t>& written, const char* kind, const XMFLOAT3& value)
        {
            char text[96];
            snprintf(text, sizeof(text), "%.4f %.4f %.4f", value.x, value.y, value.z);
            auto [it, added] = written.emplace(text, static_cast<uint32_t>(written.size() + 1));
            if (added)
            {
                obj << kind << ' ' << text << '\n';
            }
            return it->second;
        };

        for (const ObjPart& part : parts)
        {
            mtl << "\nnewmtl " << part.name << "\nKd " << part.color.x << ' ' << part.color.y << ' ' << part.color.z
                << "\nKa 0 0 0\nKs 0.04 0.04 0.04\nNs " << (1.0f - part.roughness) * 200.0f + 2.0f << "\nPr " << part.roughness
                << "\nPm 0\nKe " << part.emissive.x << ' ' << part.emissive.y << ' ' << part.emissive.z << "\nd 1\nillum 2\n";

            std::vector<std::pair<uint32_t, uint32_t>> corners; // Position and normal of every vertex.
            obj << "\no " << part.name << '\n';
            for (const Vertex& vertex : part.mesh.vertices)
            {
                XMFLOAT3 p, n;
                XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&vertex.position), transform));
                XMStoreFloat3(&n, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&vertex.normal), transform)));
                corners.emplace_back(index(positions, "v", XMFLOAT3(p.x, p.y, -p.z)), index(normals, "vn", XMFLOAT3(n.x, n.y, -n.z)));
            }
            obj << "usemtl " << part.name << '\n';
            for (size_t i = 0; i + 2 < part.mesh.indices.size(); i += 3)
            {
                // Flipping z mirrors the triangles, so their corners go the other way round.
                obj << 'f';
                for (uint32_t corner : { part.mesh.indices[i], part.mesh.indices[i + 2], part.mesh.indices[i + 1] })
                {
                    obj << ' ' << corners[corner].first << "//" << corners[corner].second;
                }
                obj << '\n';
            }
        }
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
