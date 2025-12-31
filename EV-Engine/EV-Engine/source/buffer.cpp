#include <dx12_includes.h>

#include <buffer.h>

Buffer::Buffer(const D3D12_RESOURCE_DESC& resDesc)
	: Resource(resDesc)
{
}

Buffer::Buffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
	: Resource(resource)
{
}
