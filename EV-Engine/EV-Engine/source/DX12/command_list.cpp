#include <DX12/dx12_includes.h>

#include <DX12/command_list.h>

#include <core/application.h>
// #include <ByteAddressBuffer.h>
// #include <ConstantBuffer.h>
#include <DX12/command_queue.h>
#include <DX12/dynamic_descriptor_heap.h>
// #include <GenerateMipsPSO.h>
#include <resources/index_buffer.h>
// #include <PanoToCubemapPSO.h>
#include <DX12/render_target.h>
#include <resources/resource.h>
#include <DX12/resource_state_tracker.h>
#include <DX12/root_signature.h>
// #include <StructuredBuffer.h>
#include "resources/texture.h"
#include <DX12/upload_buffer.h>

#include "resources/buffer.h"
#include "DX12/generate_mips_pso.h"
#include "resources/material.h"
#include "resources/mesh.h"
#include "DX12/pipeline_state_object.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "DX12/shader_resource_view.h"
#include "resources/texture_usage.h"
#include "resources/vertex_buffer.h"
#include "resources/vertex_types.h"
#include "utility/helpers.h"

using namespace EV;

std::map<std::wstring, ID3D12Resource* > CommandList::m_textureCache;
std::mutex CommandList::m_textureCacheMutex;

CommandList::CommandList(D3D12_COMMAND_LIST_TYPE type)
	: m_commandListType(type)
{
	auto device = Application::Get().GetDevice();

	ThrowIfFailed(device->CreateCommandAllocator(m_commandListType, IID_PPV_ARGS(&m_commandAllocator)));

	ThrowIfFailed(device->CreateCommandList(0, m_commandListType, m_commandAllocator.Get(),
		nullptr, IID_PPV_ARGS(&m_commandList)));

	m_uploadBuffer = std::make_unique<UploadBuffer>();

	m_resourceStateTracker = std::make_unique<ResourceStateTracker>();

	for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_dynamicDescriptorHeap[i] = std::make_unique<DynamicDescriptorHeap>(static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(i));
		m_descriptorHeaps[i] = nullptr;
	}
}

CommandList::~CommandList()
{
}

void CommandList::TransitionBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES stateAfter,
	UINT subresource, bool flushBarriers)
{
	if (resource)
	{
		// The "before" state is not important. It will be resolved by the resource state tracker.
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), D3D12_RESOURCE_STATE_COMMON, stateAfter,
			subresource);
		m_resourceStateTracker->ResourceBarrier(barrier);
	}

	if (flushBarriers)
	{
		FlushResourceBarriers();
	}
}
void CommandList::TransitionBarrier(const std::shared_ptr<Resource>& resource, D3D12_RESOURCE_STATES stateAfter,
	UINT subresource, bool flushBarriers)
{
	if (resource)
	{
		TransitionBarrier(resource->GetD3D12Resource(), stateAfter, subresource, flushBarriers);
	}
}


void CommandList::UAVBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, bool flushBarriers)
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.Get());

	m_resourceStateTracker->ResourceBarrier(barrier);

	if (flushBarriers)
	{
		FlushResourceBarriers();
	}
}

void CommandList::UAVBarrier(const std::shared_ptr<Resource>& resource, bool flushBarriers)
{
	auto d3d12Resource = resource ? resource->GetD3D12Resource() : nullptr;
	UAVBarrier(d3d12Resource, flushBarriers);
}

void CommandList::AliasingBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> beforeResource,
	Microsoft::WRL::ComPtr<ID3D12Resource> afterResource, bool flushBarriers)
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::Aliasing(beforeResource.Get(), afterResource.Get());

	m_resourceStateTracker->ResourceBarrier(barrier);

	if (flushBarriers)
	{
		FlushResourceBarriers();
	}
}

void CommandList::AliasingBarrier(const std::shared_ptr<Resource>& beforeResource,
	const std::shared_ptr<Resource>& afterResource, bool flushBarriers)
{
	auto d3d12BeforeResource = beforeResource ? beforeResource->GetD3D12Resource() : nullptr;
	auto d3d12AfterResource = afterResource ? afterResource->GetD3D12Resource() : nullptr;

	AliasingBarrier(d3d12BeforeResource, d3d12AfterResource, flushBarriers);
}


void CommandList::FlushResourceBarriers()
{
	m_resourceStateTracker->FlushResourceBarriers(shared_from_this());
}

void CommandList::CopyResource(Microsoft::WRL::ComPtr<ID3D12Resource> dstRes,
	Microsoft::WRL::ComPtr<ID3D12Resource> srcRes)
{
	assert(dstRes);
	assert(srcRes);

	// Copy queues can only transition to/from COMMON state
	// Direct/Compute queues can use COPY_DEST state
	TransitionBarrier(dstRes, m_commandListType == D3D12_COMMAND_LIST_TYPE_COPY ?
		D3D12_RESOURCE_STATE_COMMON :
		D3D12_RESOURCE_STATE_COPY_DEST);
	TransitionBarrier(srcRes, m_commandListType == D3D12_COMMAND_LIST_TYPE_COPY ?
		D3D12_RESOURCE_STATE_COMMON :
		D3D12_RESOURCE_STATE_COPY_SOURCE);

	FlushResourceBarriers();

	m_commandList->CopyResource(dstRes.Get(), srcRes.Get());

	TrackResource(dstRes);
	TrackResource(srcRes);
}

void CommandList::CopyResource(const std::shared_ptr<Resource>& dstRes, const std::shared_ptr<Resource>& srcRes)
{
	assert(dstRes && srcRes);

	CopyResource(dstRes->GetD3D12Resource(), srcRes->GetD3D12Resource());
}

void CommandList::ResolveSubresource(const std::shared_ptr<Resource>& dstRes, const std::shared_ptr<Resource>& srcRes,
	uint32_t dstSubresource, uint32_t srcSubresource)
{
	assert(dstRes && srcRes);

	TransitionBarrier(dstRes, D3D12_RESOURCE_STATE_RESOLVE_DEST, dstSubresource);
	TransitionBarrier(srcRes, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, srcSubresource);

	FlushResourceBarriers();

	m_commandList->ResolveSubresource(dstRes->GetD3D12Resource().Get(), dstSubresource,
		srcRes->GetD3D12Resource().Get(), srcSubresource,
		dstRes->GetD3D12ResourceDesc().Format);

	TrackResource(srcRes);
	TrackResource(dstRes);
}


ComPtr<ID3D12Resource> CommandList::CopyBuffer(size_t bufferSize,
                                               const void* bufferData, D3D12_RESOURCE_FLAGS flags)
{
	auto device = Application::Get().GetDevice();


	ComPtr<ID3D12Resource> d3d12Resource;
	if (bufferSize == 0)
	{
		// This will result in a NULL resource (which may be desired to define a default null resource).
	}
	else
	{
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&d3d12Resource)));

		// Add the resource to the global resource state tracker.
		ResourceStateTracker::AddGlobalResourceState(d3d12Resource.Get(), D3D12_RESOURCE_STATE_COMMON);

		if (bufferData != nullptr)
		{

			CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

			// Create an upload resource to use as an intermediate buffer to copy the buffer resource 
			ComPtr<ID3D12Resource> uploadResource;
			ThrowIfFailed(device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&uploadResource)));

			D3D12_SUBRESOURCE_DATA subresourceData = {};
			subresourceData.pData = bufferData;
			subresourceData.RowPitch = bufferSize;
			subresourceData.SlicePitch = subresourceData.RowPitch;

			m_resourceStateTracker->TransitionResource(d3d12Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
			FlushResourceBarriers();

			UpdateSubresources(m_commandList.Get(), d3d12Resource.Get(),
				uploadResource.Get(), 0, 0, 1, &subresourceData);

			// Add references to resources so they stay in scope until the command list is reset.
			TrackResource(uploadResource);
		}
		TrackResource(d3d12Resource);
	}

	// buffer.SetD3D12Resource(d3d12Resource);
	// buffer.CreateViews(numElements, elementSize);
	return d3d12Resource;
}

std::shared_ptr<VertexBuffer> CommandList::CopyVertexBuffer(size_t numVertices, size_t vertexStride,
	const void* vertexBufferData)
{
	auto                          d3d12Resource = CopyBuffer(numVertices * vertexStride, vertexBufferData);
	std::shared_ptr<VertexBuffer> vertexBuffer =
		Application::Get().CreateVertexBuffer(d3d12Resource, numVertices, vertexStride);

	return vertexBuffer;
}

std::shared_ptr<IndexBuffer> CommandList::CopyIndexBuffer(size_t numIndices, DXGI_FORMAT indexFormat,
	const void* indexBufferData)
{
	size_t elementSize = indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;

	auto d3d12Resource = CopyBuffer(numIndices * elementSize, indexBufferData);

	std::shared_ptr<IndexBuffer> indexBuffer = Application::Get().CreateIndexBuffer(d3d12Resource, numIndices, indexFormat);

	return indexBuffer;
}
//
// void CommandList::CopyByteAddressBuffer(ByteAddressBuffer& byteAddressBuffer, size_t bufferSize, const void* bufferData)
// {
//     CopyBuffer(byteAddressBuffer, 1, bufferSize, bufferData, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
// }

// void CommandList::CopyStructuredBuffer(StructuredBuffer& structuredBuffer, size_t numElements, size_t elementSize, const void* bufferData)
// {
//     CopyBuffer(structuredBuffer, numElements, elementSize, bufferData, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
// }

void CommandList::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology)
{
	m_commandList->IASetPrimitiveTopology(primitiveTopology);
}
//
std::shared_ptr<Texture> CommandList::LoadTextureFromFile(const std::wstring& fileName, bool sRGB)
{
	std::shared_ptr<Texture> texture;
	fs::path                 filePath(fileName);
	auto path = fs::current_path();
	if (!fs::exists(filePath))
	{
		auto path = fs::current_path();
		throw std::exception("File not found.");
	}

	std::lock_guard<std::mutex> lock(m_textureCacheMutex);
	auto                        iter = m_textureCache.find(fileName);
	if (iter != m_textureCache.end())
	{
		texture = Application::Get().CreateTexture(iter->second);
	}
	else
	{
		TexMetadata  metadata;
		ScratchImage scratchImage;

		if (filePath.extension() == ".dds")
		{
			ThrowIfFailed(LoadFromDDSFile(fileName.c_str(), DDS_FLAGS_FORCE_RGB, &metadata, scratchImage));
		}
		else if (filePath.extension() == ".hdr")
		{
			ThrowIfFailed(LoadFromHDRFile(fileName.c_str(), &metadata, scratchImage));
		}
		else if (filePath.extension() == ".tga")
		{
			ThrowIfFailed(LoadFromTGAFile(fileName.c_str(), &metadata, scratchImage));
		}
		else
		{
			ThrowIfFailed(LoadFromWICFile(fileName.c_str(), WIC_FLAGS_FORCE_RGB, &metadata, scratchImage));
		}

		// Force the texture format to be sRGB to convert to linear when sampling the texture in a shader.
		if (sRGB)
		{
			metadata.format = MakeSRGB(metadata.format);
		}

		D3D12_RESOURCE_DESC textureDesc = {};
		switch (metadata.dimension)
		{
		case TEX_DIMENSION_TEXTURE1D:
			textureDesc = CD3DX12_RESOURCE_DESC::Tex1D(metadata.format, static_cast<UINT64>(metadata.width),
				static_cast<UINT16>(metadata.arraySize));
			break;
		case TEX_DIMENSION_TEXTURE2D:
			textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(metadata.format, static_cast<UINT64>(metadata.width),
				static_cast<UINT>(metadata.height),
				static_cast<UINT16>(metadata.arraySize));
			break;
		case TEX_DIMENSION_TEXTURE3D:
			textureDesc = CD3DX12_RESOURCE_DESC::Tex3D(metadata.format, static_cast<UINT64>(metadata.width),
				static_cast<UINT>(metadata.height),
				static_cast<UINT16>(metadata.depth));
			break;
		default:
			throw std::exception("Invalid texture dimension.");
			break;
		}

		auto                                   d3d12Device = Application::Get().GetDevice();
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
		CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(d3d12Device->CreateCommittedResource(
			&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&textureResource)));

		texture = Application::Get().CreateTexture(textureResource);
		texture->SetName(fileName);

		// Update the global state tracker.
		ResourceStateTracker::AddGlobalResourceState(textureResource.Get(), D3D12_RESOURCE_STATE_COMMON);

		std::vector<D3D12_SUBRESOURCE_DATA> subresources(scratchImage.GetImageCount());
		const Image* pImages = scratchImage.GetImages();
		for (int i = 0; i < scratchImage.GetImageCount(); ++i)
		{
			auto& subresource = subresources[i];
			subresource.RowPitch = pImages[i].rowPitch;
			subresource.SlicePitch = pImages[i].slicePitch;
			subresource.pData = pImages[i].pixels;
		}

		CopyTextureSubresource(texture, 0, static_cast<uint32_t>(subresources.size()), subresources.data());

		if (subresources.size() < textureResource->GetDesc().MipLevels)
		{
			GenerateMips(texture);
		}

		// Add the texture resource to the texture cache.
		m_textureCache[fileName] = textureResource.Get();
	}

	return texture;
}

void CommandList::GenerateMips(const std::shared_ptr<Texture>& texture)
{
	if (!texture)
		return;

	auto d3d12Device = Application::Get().GetDevice();

	if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COPY)
	{
		if (!m_computeCommandList)
		{
			m_computeCommandList = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE).GetCommandList();
		}
		m_computeCommandList->GenerateMips(texture);
		return;
	}

	auto d3d12Resource = texture->GetD3D12Resource();

	// If the texture doesn't have a valid resource? Do nothing...
	if (!d3d12Resource)
		return;
	auto resourceDesc = d3d12Resource->GetDesc();

	// If the texture only has a single mip level (level 0)
	// do nothing.
	if (resourceDesc.MipLevels == 1)
		return;
	// Currently, only non-multi-sampled 2D textures are supported.
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || resourceDesc.DepthOrArraySize != 1 ||
		resourceDesc.SampleDesc.Count > 1)
	{
		throw std::exception("GenerateMips is only supported for non-multi-sampled 2D Textures.");
	}

	ComPtr<ID3D12Resource> uavResource = d3d12Resource;
	// Create an alias of the original resource.
	// This is done to perform a GPU copy of resources with different formats.
	// BGR -> RGB texture copies will fail GPU validation unless performed
	// through an alias of the BRG resource in a placed heap.
	ComPtr<ID3D12Resource> aliasResource;

	// If the passed-in resource does not allow for UAV access
	// then create a staging resource that is used to generate
	// the mipmap chain.
	if (!texture->CheckUAVSupport() || (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
	{
		// Describe an alias resource that is used to copy the original texture.
		auto aliasDesc = resourceDesc;
		// Placed resources can't be render targets or depth-stencil views.
		aliasDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		aliasDesc.Flags &= ~(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		// Describe a UAV compatible resource that is used to perform
		// mipmapping of the original texture.
		auto uavDesc = aliasDesc;  // The flags for the UAV description must match that of the alias description.
		uavDesc.Format = Texture::GetUAVCompatableFormat(resourceDesc.Format);

		D3D12_RESOURCE_DESC resourceDescs[] = { aliasDesc, uavDesc };

		// Create a heap that is large enough to store a copy of the original resource.
		auto allocationInfo = d3d12Device->GetResourceAllocationInfo(0, _countof(resourceDescs), resourceDescs);

		D3D12_HEAP_DESC heapDesc = {};
		heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
		heapDesc.Alignment = allocationInfo.Alignment;
		heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ComPtr<ID3D12Heap> heap;
		ThrowIfFailed(d3d12Device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)));

		// Make sure the heap does not go out of scope until the command list
		// is finished executing on the command queue.
		TrackResource(heap);

		// Create a placed resource that matches the description of the
		// original resource. This resource is used to copy the original
		// texture to the UAV compatible resource.
		ThrowIfFailed(d3d12Device->CreatePlacedResource(heap.Get(), 0, &aliasDesc, D3D12_RESOURCE_STATE_COMMON,
			nullptr, IID_PPV_ARGS(&aliasResource)));

		ResourceStateTracker::AddGlobalResourceState(aliasResource.Get(), D3D12_RESOURCE_STATE_COMMON);
		// Ensure the scope of the alias resource.
		TrackResource(aliasResource);

		// Create a UAV compatible resource in the same heap as the alias
		// resource.
		ThrowIfFailed(d3d12Device->CreatePlacedResource(heap.Get(), 0, &uavDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_PPV_ARGS(&uavResource)));

		ResourceStateTracker::AddGlobalResourceState(uavResource.Get(), D3D12_RESOURCE_STATE_COMMON);

		// Ensure the scope of the UAV compatible resource.
		TrackResource(uavResource);

		// Add an aliasing barrier for the alias resource.
		AliasingBarrier(nullptr, aliasResource);

		// Copy the original resource to the alias resource.
		// This ensures GPU validation.
		CopyResource(aliasResource, d3d12Resource);

		// Add an aliasing barrier for the UAV compatible resource.
		AliasingBarrier(aliasResource, uavResource);
	}

	// Generate mips with the UAV compatible resource.
	auto uavTexture = Application::Get().CreateTexture(uavResource);
	GenerateMips_UAV(uavTexture, Texture::IsSRGBFormat(resourceDesc.Format));

	if (aliasResource)
	{
		AliasingBarrier(uavResource, aliasResource);
		// Copy the alias resource back to the original resource.
		CopyResource(d3d12Resource, aliasResource);
	}

}

void CommandList::GenerateMips_UAV(const std::shared_ptr<Texture>& texture, bool isSRGB)
{
	if (!m_generateMipsPSO)
	{
		m_generateMipsPSO = std::make_unique<GenerateMipsPSO>();
	}

	SetPipelineState(m_generateMipsPSO->GetPipelineState());
	SetComputeRootSignature(m_generateMipsPSO->GetRootSignature());

	GenerateMipsCB generateMipsCB;
	generateMipsCB.IsSRGB = isSRGB;

	auto resource = texture->GetD3D12Resource();
	auto resourceDesc = resource->GetDesc();

	// Create an SRV that uses the format of the original texture.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = isSRGB ? Texture::GetSRGBFormat(resourceDesc.Format) : resourceDesc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;  // Only 2D textures are supported (this was checked in the calling function).
	srvDesc.Texture2D.MipLevels = resourceDesc.MipLevels;

	auto srv = Application::Get().CreateShaderResourceView(texture, &srvDesc);

	for (uint32_t srcMip = 0; srcMip < resourceDesc.MipLevels - 1u; )
	{
		uint64_t srcWidth = resourceDesc.Width >> srcMip;
		uint32_t srcHeight = resourceDesc.Height >> srcMip;
		uint32_t dstWidth = static_cast<uint32_t>(srcWidth >> 1);
		uint32_t dstHeight = srcHeight >> 1;

		// 0b00(0): Both width and height are even.
		// 0b01(1): Width is odd, height is even.
		// 0b10(2): Width is even, height is odd.
		// 0b11(3): Both width and height are odd.
		generateMipsCB.SrcDimension = (srcHeight & 1) << 1 | (srcWidth & 1);

		// How many mipmap levels to compute this pass (max 4 mips per pass)
		DWORD mipCount;

		// The number of times we can half the size of the texture and get
		// exactly a 50% reduction in size.
		// A 1 bit in the width or height indicates an odd dimension.
		// The case where either the width or the height is exactly 1 is handled
		// as a special case (as the dimension does not require reduction).
		_BitScanForward(&mipCount,
			(dstWidth == 1 ? dstHeight : dstWidth) | (dstHeight == 1 ? dstWidth : dstHeight));
		// Maximum number of mips to generate is 4.
		mipCount = std::min<DWORD>(4, mipCount + 1);
		// Clamp to total number of mips left over.
		mipCount = (srcMip + mipCount) >= resourceDesc.MipLevels ? resourceDesc.MipLevels - srcMip - 1 : mipCount;

		// Dimensions should not reduce to 0.
		// This can happen if the width and height are not the same.
		dstWidth = std::max<DWORD>(1, dstWidth);
		dstHeight = std::max<DWORD>(1, dstHeight);

		generateMipsCB.SrcMipLevel = srcMip;
		generateMipsCB.NumMipLevels = mipCount;
		generateMipsCB.TexelSize.x = 1.0f / (float)dstWidth;
		generateMipsCB.TexelSize.y = 1.0f / (float)dstHeight;

		SetCompute32BitConstants(GenerateMips::GenerateMipsCB, generateMipsCB);

		SetShaderResourceView(GenerateMips::SrcMip, 0, srv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcMip,
			1);

		for (uint32_t mip = 0; mip < mipCount; ++mip)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = resourceDesc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = srcMip + mip + 1;

			auto uav = Application::Get().CreateUnorderedAccessView(texture, nullptr, &uavDesc);
			SetUnorderedAccessView(GenerateMips::OutMip, mip, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				srcMip + mip + 1, 1);
		}

		// Pad any unused mip levels with a default UAV. Doing this keeps the DX12 runtime happy.
		if (mipCount < 4)
		{
			m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
				GenerateMips::OutMip, mipCount, 4 - mipCount, m_generateMipsPSO->GetDefaultUAV());
		}

		Dispatch(Math::DivideByMultiple(dstWidth, 8), Math::DivideByMultiple(dstHeight, 8));

		UAVBarrier(texture);

		srcMip += mipCount;
	}
}
//
// void CommandList::GenerateMips_BGR(Texture& texture)
// {
//     auto device = Application::Get().GetDevice();
//
//     auto resource = texture.GetD3D12Resource();
//     auto resourceDesc = resource->GetDesc();
//
//     // Create a new resource with a UAV compatible format and UAV flags.
//     auto copyDesc = resourceDesc;
//     copyDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
//     copyDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
//
//     // Create a heap to alias the resource. This is used to copy the resource without 
//     // failing GPU validation.
//     auto allocationInfo = device->GetResourceAllocationInfo(0, 1, &resourceDesc);
//     auto bufferSize = GetRequiredIntermediateSize(resource.Get(), 0, resourceDesc.MipLevels);
//
//     D3D12_HEAP_DESC heapDesc = {};
//     heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
//     heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
//     heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
//     heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
//     heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
//
//     ComPtr<ID3D12Heap> heap;
//     ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)));
//
//     ComPtr<ID3D12Resource> resourceCopy;
//     ThrowIfFailed(device->CreatePlacedResource(
//         heap.Get(),
//         0,
//         &copyDesc,
//         D3D12_RESOURCE_STATE_COMMON,
//         nullptr,
//         IID_PPV_ARGS(&resourceCopy)
//     ));
//
//     ResourceStateTracker::AddGlobalResourceState(resourceCopy.Get(), D3D12_RESOURCE_STATE_COMMON);
//
//     Texture copyTexture(resourceCopy);
//
//     // Create an alias for which to perform the copy operation.
//     auto aliasDesc = resourceDesc;
//     aliasDesc.Format = (resourceDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
//         resourceDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) ?
//         DXGI_FORMAT_B8G8R8X8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
//
//     ComPtr<ID3D12Resource> aliasCopy;
//     ThrowIfFailed(device->CreatePlacedResource(
//         heap.Get(),
//         0,
//         &aliasDesc,
//         D3D12_RESOURCE_STATE_COMMON,
//         nullptr,
//         IID_PPV_ARGS(&aliasCopy)
//     ));
//
//     ResourceStateTracker::AddGlobalResourceState(aliasCopy.Get(), D3D12_RESOURCE_STATE_COMMON);
//
//     // Copy the original texture to the aliased texture.
//     Texture aliasTexture(aliasCopy);
//     AliasingBarrier(Texture(), aliasTexture); // There is no "before" texture. 
//     // Default constructed Texture is equivalent to a "null" texture.
//     CopyResource(aliasTexture, texture);
//
//     // Alias the UAV compatible texture back.
//     AliasingBarrier(aliasTexture, copyTexture);
//     // Now use the resource copy to generate the mips.
//     GenerateMips_UAV(copyTexture);
//
//     // Copy back to the original (via the alias to ensure GPU validation)
//     AliasingBarrier(copyTexture, aliasTexture);
//     CopyResource(texture, aliasTexture);
//
//     // Track resource to ensure the lifetime.
//     TrackObject(heap);
//     TrackResource(copyTexture);
//     TrackResource(aliasTexture);
//     TrackResource(texture);
// }

void CommandList::GenerateMips_sRGB(Texture& texture)
{
	// auto device = Application::Get().GetDevice();
	//
	// // Create a UAV compatible texture.
	// auto resource = texture.GetD3D12Resource();
	// auto resourceDesc = resource->GetDesc();
	//
	// // Create a copy of the original texture with UAV compatible format and UAV flags.
	// auto copyDesc = resourceDesc;
	// copyDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	// copyDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	//
	// // Create the resource as a placed resource in a heap to perform an aliased copy.
	// // Create a heap to alias the resource. This is used to copy the resource without 
	// // failing GPU validation.
	// auto allocationInfo = device->GetResourceAllocationInfo(0, 1, &resourceDesc);
	// auto bufferSize = GetRequiredIntermediateSize(resource.Get(), 0, resourceDesc.MipLevels);
	//
	// D3D12_HEAP_DESC heapDesc = {};
	// heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
	// heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
	// heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	// heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	// heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	//
	// ComPtr<ID3D12Heap> heap;
	// ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)));
	//
	// ComPtr<ID3D12Resource> resourceCopy;
	// ThrowIfFailed(device->CreatePlacedResource(
	//     heap.Get(),
	//     0,
	//     &copyDesc,
	//     D3D12_RESOURCE_STATE_COMMON,
	//     nullptr,
	//     IID_PPV_ARGS(&resourceCopy)
	// ));
	//
	// ResourceStateTracker::AddGlobalResourceState(resourceCopy.Get(), D3D12_RESOURCE_STATE_COMMON);
	//
	// Texture copyTexture(resourceCopy);
	//
	// // Create an alias for which to perform the copy operation.
	// auto aliasDesc = resourceDesc;
	//
	// ComPtr<ID3D12Resource> aliasCopy;
	// ThrowIfFailed(device->CreatePlacedResource(
	//     heap.Get(),
	//     0,
	//     &aliasDesc,
	//     D3D12_RESOURCE_STATE_COMMON,
	//     nullptr,
	//     IID_PPV_ARGS(&aliasCopy)
	// ));
	//
	// ResourceStateTracker::AddGlobalResourceState(aliasCopy.Get(), D3D12_RESOURCE_STATE_COMMON);
	//
	// // Copy the original texture to the aliased texture.
	// Texture aliasTexture(aliasCopy);
	// AliasingBarrier(Texture(), aliasTexture); // There is no "before" texture. 
	// // Default constructed Texture is equivalent to a "null" texture.
	// CopyResource(aliasTexture, texture);
	//
	// // Alias the UAV compatible texture back.
	// AliasingBarrier(aliasTexture, copyTexture);
	// // Now use the resource copy to generate the mips.
	// GenerateMips_UAV(copyTexture);
	//
	// // Copy back to the original (via the alias to ensure GPU validation)
	// AliasingBarrier(copyTexture, aliasTexture);
	// CopyResource(texture, aliasTexture);
	//
	// // Track resource to ensure the lifetime.
	// TrackObject(heap);
	// TrackResource(copyTexture);
	// TrackResource(aliasTexture);
	// TrackResource(texture);
}
//
// void CommandList::PanoToCubemap(Texture& cubemapTexture, const Texture& panoTexture)
// {
//     if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COPY)
//     {
//         if (!m_ComputeCommandList)
//         {
//             m_ComputeCommandList = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->GetCommandList();
//         }
//         m_ComputeCommandList->PanoToCubemap(cubemapTexture, panoTexture);
//         return;
//     }
//
//     if (!m_PanoToCubemapPSO)
//     {
//         m_PanoToCubemapPSO = std::make_unique<PanoToCubemapPSO>();
//     }
//
//     auto device = Application::Get().GetDevice();
//
//     auto cubemapResource = cubemapTexture.GetD3D12Resource();
//     if (!cubemapResource) return;
//
//     CD3DX12_RESOURCE_DESC cubemapDesc(cubemapResource->GetDesc());
//
//     auto stagingResource = cubemapResource;
//     Texture stagingTexture(stagingResource);
//     // If the passed-in resource does not allow for UAV access
//     // then create a staging resource that is used to generate
//     // the cubemap.
//     if ((cubemapDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
//     {
//         auto stagingDesc = cubemapDesc;
//         stagingDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
//
//         ThrowIfFailed(device->CreateCommittedResource(
//             &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
//             D3D12_HEAP_FLAG_NONE,
//             &stagingDesc,
//             D3D12_RESOURCE_STATE_COPY_DEST,
//             nullptr,
//             IID_PPV_ARGS(&stagingResource)
//
//         ));
//
//         ResourceStateTracker::AddGlobalResourceState(stagingResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
//
//         stagingTexture.SetD3D12Resource(stagingResource);
//         stagingTexture.CreateViews();
//         stagingTexture.SetName(L"Pano to Cubemap Staging Texture");
//
//         CopyResource(stagingTexture, cubemapTexture);
//     }
//
//     TransitionBarrier(stagingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
//
//     m_commandList->SetPipelineState(m_PanoToCubemapPSO->GetPipelineState().Get());
//     SetComputeRootSignature(m_PanoToCubemapPSO->GetRootSignature());
//
//     PanoToCubemapCB panoToCubemapCB;
//
//     D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
//     uavDesc.Format = cubemapDesc.Format;
//     uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
//     uavDesc.Texture2DArray.FirstArraySlice = 0;
//     uavDesc.Texture2DArray.ArraySize = 6;
//
//     for (uint32_t mipSlice = 0; mipSlice < cubemapDesc.MipLevels; )
//     {
//         // Maximum number of mips to generate per pass is 5.
//         uint32_t numMips = std::min<uint32_t>(5, cubemapDesc.MipLevels - mipSlice);
//
//         panoToCubemapCB.FirstMip = mipSlice;
//         panoToCubemapCB.CubemapSize = std::max<uint32_t>(static_cast<uint32_t>(cubemapDesc.Width), cubemapDesc.Height) >> mipSlice;
//         panoToCubemapCB.NumMips = numMips;
//
//         SetCompute32BitConstants(PanoToCubemapRS::PanoToCubemapCB, panoToCubemapCB);
//
//         SetShaderResourceView(PanoToCubemapRS::SrcTexture, 0, panoTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
//
//         for (uint32_t mip = 0; mip < numMips; ++mip)
//         {
//             uavDesc.Texture2DArray.MipSlice = mipSlice + mip;
//             SetUnorderedAccessView(PanoToCubemapRS::DstMips, mip, stagingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, 0, &uavDesc);
//         }
//
//         if (numMips < 5)
//         {
//             // Pad unused mips. This keeps DX12 runtime happy.
//             m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(PanoToCubemapRS::DstMips, panoToCubemapCB.NumMips, 5 - numMips, m_PanoToCubemapPSO->GetDefaultUAV());
//         }
//
//         Dispatch(Math::DivideByMultiple(panoToCubemapCB.CubemapSize, 16), Math::DivideByMultiple(panoToCubemapCB.CubemapSize, 16), 6);
//
//         mipSlice += numMips;
//     }
//
//     if (stagingResource != cubemapResource)
//     {
//         CopyResource(cubemapTexture, stagingTexture);
//     }
// }

std::shared_ptr<Scene> CommandList::CreateSphere(float radius, uint32_t tessellation, bool reversWinding)
{

	if (tessellation < 3)
		throw std::out_of_range("tessellation parameter out of range");

	VertexCollection vertices;
	IndexCollection  indices;

	size_t verticalSegments = tessellation;
	size_t horizontalSegments = tessellation * 2;

	// Create rings of vertices at progressively higher latitudes.
	for (size_t i = 0; i <= verticalSegments; i++)
	{
		float v = 1 - (float)i / verticalSegments;

		float latitude = (i * XM_PI / verticalSegments) - XM_PIDIV2;
		float dy, dxz;

		XMScalarSinCos(&dy, &dxz, latitude);

		// Create a single ring of vertices at this latitude.
		for (size_t j = 0; j <= horizontalSegments; j++)
		{
			float u = (float)j / horizontalSegments;

			float longitude = j * XM_2PI / horizontalSegments;
			float dx, dz;

			XMScalarSinCos(&dx, &dz, longitude);

			dx *= dxz;
			dz *= dxz;

			auto normal = XMVectorSet(dx, dy, dz, 0);
			auto textureCoordinate = XMVectorSet(u, v, 0, 0);
			auto position = normal * radius;

			vertices.emplace_back(position, normal, textureCoordinate);
		}
	}

	// Fill the index buffer with triangles joining each pair of latitude rings.
	size_t stride = horizontalSegments + 1;

	for (size_t i = 0; i < verticalSegments; i++)
	{
		for (size_t j = 0; j <= horizontalSegments; j++)
		{
			size_t nextI = i + 1;
			size_t nextJ = (j + 1) % stride;

			indices.push_back(i * stride + nextJ);
			indices.push_back(nextI * stride + j);
			indices.push_back(i * stride + j);

			indices.push_back(nextI * stride + nextJ);
			indices.push_back(nextI * stride + j);
			indices.push_back(i * stride + nextJ);
		}
	}

	if (reversWinding)
	{
		ReverseWinding(indices, vertices);
	}

	return CreateScene(vertices, indices);
}

std::shared_ptr<Scene> CommandList::CreatePlane(float width, float depth, uint32_t subdivisionWidth, uint32_t subdivisionDepth, bool reverseWinding)
{
	if (subdivisionWidth < 1 || subdivisionDepth < 1)
		throw std::out_of_range("subdivision parameters must be at least 1");

	VertexCollection vertices;
	IndexCollection  indices;

	float halfWidth = width * 0.5f;
	float halfDepth = depth * 0.5f;

	float dx = width / subdivisionWidth;
	float dz = depth / subdivisionDepth;

	float du = 1.0f / subdivisionWidth;
	float dv = 1.0f / subdivisionDepth;

	// Plane lies in XZ plane, Y-up
	XMFLOAT3 normal = { 0.0f, 1.0f, 0.0f };
	XMFLOAT3 tangent = { 1.0f, 0.0f, 0.0f };    // Along X-axis
	XMFLOAT3 bitangent = { 0.0f, 0.0f, 1.0f };  // Along Z-axis

	// Generate vertices
	for (uint32_t i = 0; i <= subdivisionDepth; ++i)
	{
		float z = halfDepth - i * dz;
		float v = (float)i * dv;

		for (uint32_t j = 0; j <= subdivisionWidth; ++j)
		{
			float x = -halfWidth + j * dx;
			float u = (float)j * du;

			XMFLOAT3 position = { x, 0.0f, z };
			XMFLOAT3 texCoord = { u, v, 0.0f };

			vertices.emplace_back(position, normal, texCoord, tangent, bitangent);
		}
	}

	// Generate indices
	uint32_t stride = subdivisionWidth + 1;

	for (uint32_t i = 0; i < subdivisionDepth; ++i)
	{
		for (uint32_t j = 0; j < subdivisionWidth; ++j)
		{
			uint32_t bottomLeft = i * stride + j;
			uint32_t bottomRight = bottomLeft + 1;
			uint32_t topLeft = (i + 1) * stride + j;
			uint32_t topRight = topLeft + 1;

			// First triangle
			indices.emplace_back(bottomLeft);
			indices.emplace_back(topLeft);
			indices.emplace_back(bottomRight);

			// Second triangle
			indices.emplace_back(bottomRight);
			indices.emplace_back(topLeft);
			indices.emplace_back(topRight);
		}
	}

	if (reverseWinding)
	{
		ReverseWinding(indices, vertices);
	}

	return CreateScene(vertices, indices);
}

void CommandList::ClearTexture(const std::shared_ptr<Texture>& texture, float clearColor[])
{
	assert(texture);

	TransitionBarrier(texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);
	m_commandList->ClearRenderTargetView(texture->GetRenderTargetView(), clearColor, 0, nullptr);

	TrackResource(texture);
}

void CommandList::ClearDepthStencilTexture(const std::shared_ptr<Texture>& texture, D3D12_CLEAR_FLAGS clearFlags,
	float depth, uint8_t stencil)
{
	assert(texture);

	TransitionBarrier(texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);
	m_commandList->ClearDepthStencilView(texture->GetDepthStencilView(), clearFlags, depth, stencil, 0, nullptr);

	TrackResource(texture);
}

void CommandList::CopyTextureSubresource(const std::shared_ptr<Texture>& texture, uint32_t firstSubresource,
	uint32_t numSubresources, D3D12_SUBRESOURCE_DATA* subresourceData)
{
	assert(texture);

	auto device = Application::Get().GetDevice();
	auto destinationResource = texture->GetD3D12Resource();

	if (destinationResource)
	{
		// Copy queues can only transition to/from COMMON state
		// Direct/Compute queues can use COPY_DEST state
		if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COPY)
		{
			// On Copy queues, resources must remain in COMMON state
			// The COMMON state is implicitly compatible with copy operations
			TransitionBarrier(texture, D3D12_RESOURCE_STATE_COMMON);
		}
		else
		{
			// On Direct/Compute queues, we can use the optimal COPY_DEST state
			TransitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST);
		}
		FlushResourceBarriers();

		UINT64 requiredSize =
			GetRequiredIntermediateSize(destinationResource.Get(), firstSubresource, numSubresources);

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(requiredSize);

		// Create a temporary (intermediate) resource for uploading the subresources
		ComPtr<ID3D12Resource> intermediateResource;
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&intermediateResource)
		));

		UpdateSubresources(m_commandList.Get(), destinationResource.Get(), intermediateResource.Get(), 0,
			firstSubresource, numSubresources, subresourceData);

		TrackResource(intermediateResource);
		TrackResource(destinationResource);
	}
}


void CommandList::SetGraphicsDynamicConstantBuffer(uint32_t rootParameterIndex, size_t sizeInBytes, const void* bufferData)
{
	// Constant buffers must be 256-byte aligned.
	auto heapAllococation = m_uploadBuffer->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(heapAllococation.CPU, bufferData, sizeInBytes);

	m_commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, heapAllococation.GPU);
}

void CommandList::SetGraphics32BitConstants(uint32_t rootParameterIndex, uint32_t numConstants, const void* constants)
{
	m_commandList->SetGraphicsRoot32BitConstants(rootParameterIndex, numConstants, constants, 0);
}

void CommandList::SetCompute32BitConstants(uint32_t rootParameterIndex, uint32_t numConstants, const void* constants)
{
	m_commandList->SetComputeRoot32BitConstants(rootParameterIndex, numConstants, constants, 0);
}

// void CommandList::SetVertexBuffer(uint32_t slot, const VertexBuffer& vertexBuffer)
// {
// 	TransitionBarrier(&vertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
//
// 	auto vertexBufferView = vertexBuffer.GetVertexBufferView();
//
// 	m_commandList->IASetVertexBuffers(slot, 1, &vertexBufferView);
//
// 	TrackResource(vertexBuffer);
// }

void CommandList::SetVertexBuffers(uint32_t                                          startSlot,
	const std::vector<std::shared_ptr<VertexBuffer>>& vertexBuffers)
{
	std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
	views.reserve(vertexBuffers.size());

	for (auto vertexBuffer : vertexBuffers)
	{
		if (vertexBuffer)
		{
			TransitionBarrier(vertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			TrackResource(vertexBuffer);

			views.push_back(vertexBuffer->GetVertexBufferView());
		}
	}

	m_commandList->IASetVertexBuffers(startSlot, static_cast<UINT>(views.size()), views.data());
}

void CommandList::SetVertexBuffer(uint32_t slot, const std::shared_ptr<VertexBuffer>& vertexBuffer)
{
	SetVertexBuffers(slot, { vertexBuffer });
}

void CommandList::SetDynamicVertexBuffer(uint32_t slot, size_t numVertices, size_t vertexSize, const void* vertexBufferData)
{
	size_t bufferSize = numVertices * vertexSize;

	auto heapAllocation = m_uploadBuffer->Allocate(bufferSize, vertexSize);
	memcpy(heapAllocation.CPU, vertexBufferData, bufferSize);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	vertexBufferView.BufferLocation = heapAllocation.GPU;
	vertexBufferView.SizeInBytes = static_cast<UINT>(bufferSize);
	vertexBufferView.StrideInBytes = static_cast<UINT>(vertexSize);

	m_commandList->IASetVertexBuffers(slot, 1, &vertexBufferView);
}

void CommandList::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer)
{
	if (indexBuffer)
	{
		TransitionBarrier(indexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		TrackResource(indexBuffer);
		auto indexBufferView = indexBuffer->GetIndexBufferView();
		m_commandList->IASetIndexBuffer(&indexBufferView);

		// m_commandList->IASetIndexBuffer(&(indexBuffer->GetIndexBufferView()));
	}
}

void CommandList::SetDynamicIndexBuffer(size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData)
{
	size_t indexSizeInBytes = indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
	size_t bufferSize = numIndicies * indexSizeInBytes;

	auto heapAllocation = m_uploadBuffer->Allocate(bufferSize, indexSizeInBytes);
	memcpy(heapAllocation.CPU, indexBufferData, bufferSize);

	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
	indexBufferView.BufferLocation = heapAllocation.GPU;
	indexBufferView.SizeInBytes = static_cast<UINT>(bufferSize);
	indexBufferView.Format = indexFormat;

	m_commandList->IASetIndexBuffer(&indexBufferView);
}

void CommandList::SetGraphicsDynamicStructuredBuffer(uint32_t slot, size_t numElements, size_t elementSize, const void* bufferData)
{
	size_t bufferSize = numElements * elementSize;

	auto heapAllocation = m_uploadBuffer->Allocate(bufferSize, elementSize);

	memcpy(heapAllocation.CPU, bufferData, bufferSize);

	m_commandList->SetGraphicsRootShaderResourceView(slot, heapAllocation.GPU);
}
void CommandList::SetViewport(const D3D12_VIEWPORT& viewport)
{
	SetViewports({ viewport });
}

void CommandList::SetViewports(const std::vector<D3D12_VIEWPORT>& viewports)
{
	assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_commandList->RSSetViewports(static_cast<UINT>(viewports.size()),
		viewports.data());
}

void CommandList::SetScissorRect(const D3D12_RECT& scissorRect)
{
	SetScissorRects({ scissorRect });
}

void CommandList::SetScissorRects(const std::vector<D3D12_RECT>& scissorRects)
{
	assert(scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_commandList->RSSetScissorRects(static_cast<UINT>(scissorRects.size()),
		scissorRects.data());
}

void CommandList::SetPipelineState(const std::shared_ptr<PipelineStateObject>& pipelineState)
{
	assert(pipelineState);

	auto d3d12PipelineStateObject = pipelineState->GetD3D12PipelineState().Get();
	if (m_pipelineState != d3d12PipelineStateObject)
	{
		m_pipelineState = d3d12PipelineStateObject;

		m_commandList->SetPipelineState(d3d12PipelineStateObject);

		TrackResource(d3d12PipelineStateObject);
	}
}

void CommandList::SetGraphicsRootSignature(const std::shared_ptr<RootSignature>& rootSignature)
{
	auto d3d12RootSignature = rootSignature->GetRootSignature().Get();
	if (m_rootSignature != d3d12RootSignature)
	{
		m_rootSignature = d3d12RootSignature;

		for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			m_dynamicDescriptorHeap[i]->ParseRootSignature(rootSignature);
		}

		m_commandList->SetGraphicsRootSignature(m_rootSignature);

		TrackResource(m_rootSignature);
	}
}

void CommandList::SetComputeRootSignature(const std::shared_ptr<RootSignature>& rootSignature)
{
	assert(rootSignature);

	auto d3d12RootSignature = rootSignature->GetRootSignature().Get();
	if (m_rootSignature != d3d12RootSignature)
	{
		m_rootSignature = d3d12RootSignature;

		for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			m_dynamicDescriptorHeap[i]->ParseRootSignature(rootSignature);
		}

		m_commandList->SetComputeRootSignature(m_rootSignature);

		TrackResource(m_rootSignature);
	}
}

void CommandList::SetShaderResourceView(uint32_t rootParameterIndex, uint32_t descriptorOffset,
	const std::shared_ptr<ShaderResourceView>& srv,
	D3D12_RESOURCE_STATES stateAfter, UINT firstSubresource, UINT numSubresources)
{
	assert(srv);

	auto resource = srv->GetResource();
	if (resource)
	{
		if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			for (uint32_t i = 0; i < numSubresources; ++i)
			{
				TransitionBarrier(resource, stateAfter, firstSubresource + i);
			}
		}
		else
		{
			TransitionBarrier(resource, stateAfter);
		}

		TrackResource(resource);
	}

	m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
		rootParameterIndex, descriptorOffset, 1, srv->GetDescriptorHandle());
}

// void CommandList::SetShaderResourceView(int32_t rootParameterIndex, uint32_t descriptorOffset,
// 	const std::shared_ptr<Texture>& texture, D3D12_RESOURCE_STATES stateAfter,
// 	UINT firstSubresource, UINT numSubresources)
// {
// 	if (texture)
// 	{
// 		if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
// 		{
// 			for (uint32_t i = 0; i < numSubresources; ++i)
// 			{
// 				TransitionBarrier(texture, stateAfter, firstSubresource + i);
// 			}
// 		}
// 		else
// 		{
// 			TransitionBarrier(texture, stateAfter);
// 		}
//
// 		TrackResource(texture);
//
// 		m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
// 			rootParameterIndex, descriptorOffset, 1, texture->GetShaderResourceView());
// 	}
// }

void CommandList::SetShaderResourceView(uint32_t rootParameterIndex, const std::shared_ptr<Buffer>& buffer,
	D3D12_RESOURCE_STATES stateAfter, size_t bufferOffset)
{
	if (buffer)
	{
		auto d3d12Resource = buffer->GetD3D12Resource();
		TransitionBarrier(d3d12Resource, stateAfter);

		m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageInlineSRV(
			rootParameterIndex, d3d12Resource->GetGPUVirtualAddress() + bufferOffset);

		TrackResource(buffer);
	}
}

void CommandList::SetShaderResourceView(int32_t rootParameterIndex, uint32_t descriptorOffset,
	const std::shared_ptr<Texture>& texture, D3D12_RESOURCE_STATES stateAfter, UINT firstSubresource,
	UINT numSubresources)
{
	if (texture)
	{
		if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			for (uint32_t i = 0; i < numSubresources; ++i)
			{
				TransitionBarrier(texture, stateAfter, firstSubresource + i);
			}
		}
		else
		{
			TransitionBarrier(texture, stateAfter);
		}

		TrackResource(texture);

		m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
			rootParameterIndex, descriptorOffset, 1, texture->GetShaderResourceView());
	}
}


void CommandList::SetUnorderedAccessView(uint32_t rootParameterIndex, uint32_t descriptorOffset,
                                         const std::shared_ptr<UnorderedAccessView>& uav,
                                         D3D12_RESOURCE_STATES stateAfter, UINT firstSubresource,
                                         UINT numSubresources)
{
	assert(uav);

	auto resource = uav->GetResource();
	if (resource)
	{
		if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			for (uint32_t i = 0; i < numSubresources; ++i)
			{
				TransitionBarrier(resource, stateAfter, firstSubresource + i);
			}
		}
		else
		{
			TransitionBarrier(resource, stateAfter);
		}

		TrackResource(resource);
	}

	m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
		rootParameterIndex, descriptorOffset, 1, uav->GetDescriptorHandle());
}

void CommandList::SetUnorderedAccessView(uint32_t rootParameterIndex, uint32_t descriptorOffset,
	const std::shared_ptr<Texture>& texture, UINT mip,
	D3D12_RESOURCE_STATES stateAfter, UINT firstSubresource,
	UINT numSubresources)
{
	if (texture)
	{
		if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			for (uint32_t i = 0; i < numSubresources; ++i)
			{
				TransitionBarrier(texture, stateAfter, firstSubresource + i);
			}
		}
		else
		{
			TransitionBarrier(texture, stateAfter);
		}

		TrackResource(texture);

		m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
			rootParameterIndex, descriptorOffset, 1, texture->GetUnorderedAccessView(mip));
	}
}

void CommandList::SetUnorderedAccessView(uint32_t rootParameterIndex, const std::shared_ptr<Buffer>& buffer,
	D3D12_RESOURCE_STATES stateAfter, size_t bufferOffset)
{
	if (buffer)
	{
		auto d3d12Resource = buffer->GetD3D12Resource();
		TransitionBarrier(d3d12Resource, stateAfter);

		m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageInlineUAV(
			rootParameterIndex, d3d12Resource->GetGPUVirtualAddress() + bufferOffset);

		TrackResource(buffer);
	}
}


// void CommandList::SetConstantBufferView(uint32_t rootParameterIndex, uint32_t descriptorOffset,
// 	const std::shared_ptr<ConstantBufferView>& cbv,
// 	D3D12_RESOURCE_STATES                      stateAfter)
// {
// 	assert(cbv);
//
// 	auto constantBuffer = cbv->GetConstantBuffer();
// 	if (constantBuffer)
// 	{
// 		TransitionBarrier(constantBuffer, stateAfter);
// 		TrackResource(constantBuffer);
// 	}
//
// 	m_dynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
// 		rootParameterIndex, descriptorOffset, 1, cbv->GetDescriptorHandle());
// }

void CommandList::SetRenderTarget(const RenderTarget& renderTarget)
{
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
	renderTargetDescriptors.reserve(AttachmentPoint::NumAttachmentPoints);

	const auto& textures = renderTarget.GetTextures();

	// Bind color targets (max of 8 render targets can be bound to the rendering pipeline.
	for (int i = 0; i < 8; ++i)
	{
		auto texture = textures[i];

		if (texture)
		{
			TransitionBarrier(texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
			renderTargetDescriptors.push_back(texture->GetRenderTargetView());

			TrackResource(texture);
		}
	}

	auto depthTexture = renderTarget.GetTexture(AttachmentPoint::DepthStencil);

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor(D3D12_DEFAULT);
	if (depthTexture)
	{
		TransitionBarrier(depthTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		depthStencilDescriptor = depthTexture->GetDepthStencilView();

		TrackResource(depthTexture);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = depthStencilDescriptor.ptr != 0 ? &depthStencilDescriptor : nullptr;

	m_commandList->OMSetRenderTargets(static_cast<UINT>(renderTargetDescriptors.size()),
		renderTargetDescriptors.data(), FALSE, pDSV);
}

void CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
	FlushResourceBarriers();

	for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_dynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw(*this);
	}

	m_commandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
{
	FlushResourceBarriers();

	for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_dynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw(*this);
	}

	m_commandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void CommandList::Dispatch(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ)
{
	FlushResourceBarriers();

	for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_dynamicDescriptorHeap[i]->CommitStagedDescriptorsForDispatch(*this);
	}

	m_commandList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

bool CommandList::Close(const std::shared_ptr<CommandList>& pendingCommandList)
{
	// Flush any remaining barriers.
	FlushResourceBarriers();

	m_commandList->Close();

	// Flush pending resource barriers.
	uint32_t numPendingBarriers = m_resourceStateTracker->FlushPendingResourceBarriers(pendingCommandList);
	// Commit the final resource state to the global state.
	m_resourceStateTracker->CommitFinalResourceStates();

	return numPendingBarriers > 0;
}

void CommandList::Close()
{
	FlushResourceBarriers();
	m_commandList->Close();
}


void CommandList::Reset()
{
	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

	m_resourceStateTracker->Reset();
	m_uploadBuffer->Reset();

	ReleaseTrackedObjects();

	for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_dynamicDescriptorHeap[i]->Reset();
		m_descriptorHeaps[i] = nullptr;
	}

	m_rootSignature = nullptr;
	m_pipelineState = nullptr;
	m_computeCommandList = nullptr;
}

void CommandList::TrackResource(Microsoft::WRL::ComPtr<ID3D12Object> object)
{
	m_trackedObjects.push_back(object);
}

void CommandList::TrackResource(const std::shared_ptr<Resource>& res)
{
	assert(res);
	TrackResource(res->GetD3D12Resource());
}

void CommandList::ReleaseTrackedObjects()
{
	m_trackedObjects.clear();
}

void CommandList::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap)
{
	if (m_descriptorHeaps[heapType] != heap)
	{
		m_descriptorHeaps[heapType] = heap;
		BindDescriptorHeaps();
	}
}

void CommandList::BindDescriptorHeaps()
{
	UINT numDescriptorHeaps = 0;
	ID3D12DescriptorHeap* descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	for (uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		ID3D12DescriptorHeap* descriptorHeap = m_descriptorHeaps[i];
		if (descriptorHeap)
		{
			descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
		}
	}

	m_commandList->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}

std::shared_ptr<Scene> CommandList::CreateCube(float size, bool reverseWinding)
{
	// Cube is centered at 0,0,0.
	float s = size * 0.5f;

	// 8 edges of cube.
	XMFLOAT3 p[8] = { { s, s, -s }, { s, s, s },   { s, -s, s },   { s, -s, -s },
					  { -s, s, s }, { -s, s, -s }, { -s, -s, -s }, { -s, -s, s } };
	// 6 face normals
	XMFLOAT3 n[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	// 4 unique texture coordinates
	XMFLOAT3 t[4] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };

	// Indices for the vertex positions.
	uint16_t i[24] = {
		0, 1, 2, 3,  // +X
		4, 5, 6, 7,  // -X
		4, 1, 0, 5,  // +Y
		2, 7, 6, 3,  // -Y
		1, 4, 7, 2,  // +Z
		5, 0, 3, 6   // -Z
	};

	VertexCollection vertices;
	IndexCollection  indices;

	for (uint16_t f = 0; f < 6; ++f)  // For each face of the cube.
	{
		// Four vertices per face.
		vertices.emplace_back(p[i[f * 4 + 0]], n[f], t[0]);
		vertices.emplace_back(p[i[f * 4 + 1]], n[f], t[1]);
		vertices.emplace_back(p[i[f * 4 + 2]], n[f], t[2]);
		vertices.emplace_back(p[i[f * 4 + 3]], n[f], t[3]);

		// First triangle.
		indices.emplace_back(f * 4 + 0);
		indices.emplace_back(f * 4 + 1);
		indices.emplace_back(f * 4 + 2);

		// Second triangle
		indices.emplace_back(f * 4 + 2);
		indices.emplace_back(f * 4 + 3);
		indices.emplace_back(f * 4 + 0);
	}

	if (reverseWinding)
	{
		ReverseWinding(indices, vertices);
	}

	return CreateScene(vertices, indices);
}

std::shared_ptr<Scene> CommandList::LoadSceneFromFile(const std::wstring& fileName,
	const std::function<bool(float)>& loadingProgress)
{
	auto scene = std::make_shared<Scene>();

	if (scene->LoadSceneFromFile(*this, fileName, loadingProgress))
	{
		return scene;
	}

	return nullptr;
}

std::shared_ptr<Scene> CommandList::CreateScene(const VertexCollection& vertices, const IndexCollection& indices)
{
	if (vertices.empty())
	{
		return nullptr;
	}

	auto vertexBuffer = CopyVertexBuffer(vertices);
	auto indexBuffer = CopyIndexBuffer(indices);

	auto mesh = std::make_shared<Mesh>();
	// Create a default white material for new meshes.
	auto material = std::make_shared<Material>(Material::White);

	mesh->SetVertexBuffer(0, vertexBuffer);
	mesh->SetIndexBuffer(indexBuffer);
	mesh->SetMaterial(material);

	auto node = std::make_shared<SceneNode>();
	node->AddMesh(mesh);

	auto scene = std::make_shared<Scene>();
	scene->SetRootNode(node);

	return scene;
}
