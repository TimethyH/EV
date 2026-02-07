#include "DX12/dx12_includes.h"

#include <resources/vertex_buffer.h>

using namespace EV;

VertexBuffer::VertexBuffer(size_t numVertices, size_t vertexStride)
    : Buffer(CD3DX12_RESOURCE_DESC::Buffer(numVertices* vertexStride))
    , m_numVertices(numVertices)
    , m_vertexStride(vertexStride)
    , m_vertexBufferView{}
{
    CreateVertexBufferView();
}

VertexBuffer::VertexBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource, size_t numVertices,
    size_t vertexStride)
    : Buffer(resource)
    , m_numVertices(numVertices)
    , m_vertexStride(vertexStride)
    , m_vertexBufferView{}
{
    CreateVertexBufferView();
}

VertexBuffer::~VertexBuffer() {}

void VertexBuffer::CreateVertexBufferView()
{
    m_vertexBufferView.BufferLocation = m_resource->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = static_cast<UINT>(m_numVertices * m_vertexStride);
    m_vertexBufferView.StrideInBytes = static_cast<UINT>(m_vertexStride);
}
