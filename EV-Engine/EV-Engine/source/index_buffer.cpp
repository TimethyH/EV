#include "dx12_includes.h"

#include <index_buffer.h>

#include <cassert>

// using namespace dx12lib;

IndexBuffer::IndexBuffer(size_t numIndices, DXGI_FORMAT indexFormat)
    : Buffer(CD3DX12_RESOURCE_DESC::Buffer(numIndices* (indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4)))
    , m_numIndices(numIndices)
    , m_indexFormat(indexFormat)
    , m_indexBufferView{}
{
    assert(indexFormat == DXGI_FORMAT_R16_UINT || indexFormat == DXGI_FORMAT_R32_UINT);
    CreateIndexBufferView();
}

IndexBuffer::IndexBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
    size_t numIndices, DXGI_FORMAT indexFormat)
    : Buffer(resource)
    , m_numIndices(numIndices)
    , m_indexFormat(indexFormat)
    , m_indexBufferView{}
{
    assert(indexFormat == DXGI_FORMAT_R16_UINT || indexFormat == DXGI_FORMAT_R32_UINT);
    CreateIndexBufferView();
}

void IndexBuffer::CreateIndexBufferView()
{
    UINT bufferSize = m_numIndices * (m_indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4);

    m_indexBufferView.BufferLocation = m_resource->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = bufferSize;
    m_indexBufferView.Format = m_indexFormat;
}
