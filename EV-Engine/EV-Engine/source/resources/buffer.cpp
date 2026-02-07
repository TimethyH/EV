#include <DX12/dx12_includes.h>

#include <resources/buffer.h>

using namespace EV;

Buffer::Buffer(const D3D12_RESOURCE_DESC& resDesc)
	: Resource(resDesc)
{
}

Buffer::Buffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
	: Resource(resource)
{
}
