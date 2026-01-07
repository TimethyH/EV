#include "dx12_includes.h"

#include <command_list.h>
#include <index_buffer.h>
#include <mesh.h>
#include <vertex_buffer.h>
#include <Visitor.h>

// using namespace dx12lib;

Mesh::Mesh()
    : m_primitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
{
}

void Mesh::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitiveToplogy)
{
    m_primitiveTopology = primitiveToplogy;
}

D3D12_PRIMITIVE_TOPOLOGY Mesh::GetPrimitiveTopology() const
{
    return m_primitiveTopology;
}

void Mesh::SetVertexBuffer(uint32_t slotID, const std::shared_ptr<VertexBuffer>& vertexBuffer)
{
    m_vertexBuffers[slotID] = vertexBuffer;
}

std::shared_ptr<VertexBuffer> Mesh::GetVertexBuffer(uint32_t slotID) const
{
    auto iter = m_vertexBuffers.find(slotID);
    auto vertexBuffer = iter != m_vertexBuffers.end() ? iter->second : nullptr;

    return vertexBuffer;
}

void Mesh::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer)
{
    m_indexBuffer = indexBuffer;
}

std::shared_ptr<IndexBuffer> Mesh::GetIndexBuffer()
{
    return m_indexBuffer;
}

size_t Mesh::GetIndexCount() const
{
    size_t indexCount = 0;
    if (m_indexBuffer)
    {
        indexCount = m_indexBuffer->GetNumIndices();
    }

    return indexCount;
}

size_t Mesh::GetVertexCount() const
{
    size_t vertexCount = 0;

    // To count the number of vertices in the mesh, just take the number of vertices in the first vertex buffer.
    BufferMap::const_iterator iter = m_vertexBuffers.cbegin();
    if (iter != m_vertexBuffers.cend())
    {
        vertexCount = iter->second->GetNumVertices();
    }

    return vertexCount;
}

void Mesh::SetMaterial(std::shared_ptr<Material> material)
{
    m_material = material;
}

std::shared_ptr<Material> Mesh::GetMaterial() const
{
    return m_material;
}

void Mesh::Draw(CommandList& commandList, uint32_t instanceCount, uint32_t startInstance)
{
    commandList.SetPrimitiveTopology(GetPrimitiveTopology());

    for (auto vertexBuffer : m_vertexBuffers)
    {
        commandList.SetVertexBuffer(vertexBuffer.first, vertexBuffer.second);
    }

    auto indexCount = GetIndexCount();
    auto vertexCount = GetVertexCount();

    if (indexCount > 0)
    {
        commandList.SetIndexBuffer(m_indexBuffer);
        commandList.DrawIndexed(indexCount, instanceCount, 0u, 0u, startInstance);
    }
    else if (vertexCount > 0)
    {
        commandList.Draw(vertexCount, instanceCount, 0u, startInstance);
    }
}

void Mesh::Accept(Visitor& visitor)
{
    visitor.Visit(*this);
}

void Mesh::SetAABB(const DirectX::BoundingBox& aabb) {
    m_AABB = aabb;
}

const DirectX::BoundingBox& Mesh::GetAABB() const {
    return m_AABB;
}

