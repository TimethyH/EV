#include <dx12_includes.h>

#include <index_buffer.h>

#include <cassert>

IndexBuffer::IndexBuffer(const std::wstring& name)
    : Buffer(name)
    , m_numIndicies(0)
    , m_indexFormat(DXGI_FORMAT_UNKNOWN)
    , m_indexBufferView({})
{
}

IndexBuffer::~IndexBuffer()
{
}

void IndexBuffer::CreateViews(size_t numElements, size_t elementSize)
{
    assert(elementSize == 2 || elementSize == 4 && "Indices must be 16, or 32-bit integers.");

    m_numIndicies = numElements;
    m_indexFormat = (elementSize == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

    m_indexBufferView.BufferLocation = m_resource->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = static_cast<UINT>(numElements * elementSize);
    m_indexBufferView.Format = m_indexFormat;
}

D3D12_CPU_DESCRIPTOR_HANDLE IndexBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
    throw std::exception("IndexBuffer::GetShaderResourceView should not be called.");
}

D3D12_CPU_DESCRIPTOR_HANDLE IndexBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
    throw std::exception("IndexBuffer::GetUnorderedAccessView should not be called.");
}
