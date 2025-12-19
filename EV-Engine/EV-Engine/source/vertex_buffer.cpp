#include <dx12_includes.h>

#include <vertex_buffer.h>

VertexBuffer::VertexBuffer(const std::wstring& name)
    : Buffer(name)
    , m_numVertices(0)
    , m_vertexStride(0)
    , m_vertexBufferView({})
{
}

VertexBuffer::~VertexBuffer()
{
}

void VertexBuffer::CreateViews(size_t numElements, size_t elementSize)
{
    m_numVertices = numElements;
    m_vertexStride = elementSize;

    m_vertexBufferView.BufferLocation = m_resource->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = static_cast<UINT>(m_numVertices * m_vertexStride);
    m_vertexBufferView.StrideInBytes = static_cast<UINT>(m_vertexStride);
}

D3D12_CPU_DESCRIPTOR_HANDLE VertexBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
    throw std::exception("VertexBuffer::GetShaderResourceView should not be called.");
}

D3D12_CPU_DESCRIPTOR_HANDLE VertexBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
    throw std::exception("VertexBuffer::GetUnorderedAccessView should not be called.");
}