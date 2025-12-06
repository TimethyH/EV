#include "demo.h"

#include "application.h"
#include "command_queue.h"
#include "helpers.h"
#include "window.h"

#include "dx12_includes.h"
using namespace DirectX;
struct VertexData
{
	XMFLOAT3 position;
	XMFLOAT3 color;
};

static VertexData g_Vertices[8] = {
    { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
    { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
    { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
    { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
    { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
    { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
    { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
    { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
};

static WORD g_Indicies[36] =
{
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7
};

Demo::Demo(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync)
	: super(name, width, height, bVSync)
    , m_scissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX))
    , m_viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)))
    , m_fov(45.0)
    , m_contentLoaded(false)
{
	
}

void Demo::UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, ID3D12Resource** pDestination, ID3D12Resource** pIntermediate, size_t numElements, size_t elementSize, const void* pBufferData, D3D12_RESOURCE_FLAGS flags)
{
    auto device = Application::Get().GetDevice();

    size_t bufferSize = numElements * elementSize;

    ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(pDestination)));
    if (pBufferData)
    {
	    ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(pIntermediate)));
    }

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pBufferData;
    subData.RowPitch = bufferSize;
    subData.SlicePitch = subData.RowPitch;

    UpdateSubresources(pCommandList.Get(), *pDestination, *pIntermediate, 0, 0, 1, &subData);

    // https://www.3dgep.com/learning-directx-12-2/#tutorial2loadcontent
}
