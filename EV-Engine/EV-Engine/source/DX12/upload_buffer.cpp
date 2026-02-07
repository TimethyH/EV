#include <DX12/dx12_includes.h>
#include "DX12/upload_buffer.h"
#include <new>
#include "DX12/upload_buffer.h"
#include "core/application.h"
#include "utility/helpers.h"

using namespace EV;

UploadBuffer::UploadBuffer(size_t pageSize) :m_pageSize(pageSize)
{

}

size_t UploadBuffer::GetPageSize() const
{
	return m_pageSize;
}

UploadBuffer::Allocation UploadBuffer::Allocate(size_t sizeInBytes, size_t alignment)
{
	if (sizeInBytes > m_pageSize)
	{
		throw std::bad_alloc();
	}

	if (!m_currentPage || !m_currentPage->HasSpace(sizeInBytes, alignment))
	{
		m_currentPage = RequestPage();
	}

	return m_currentPage->Allocate(sizeInBytes, alignment);
}

std::shared_ptr<UploadBuffer::Page> UploadBuffer::RequestPage()
{
	std::shared_ptr<Page> page;

	// if there are pages available, return the front page
	// remove the page from available pages
	if (!m_availablePages.empty())
	{
		page = m_availablePages.front();
		m_availablePages.pop_front();
	}
	else
	{
		// if there are no pages available, we allocate one
		// we add the page to the pool
		page = std::make_shared<Page>(m_pageSize);
		m_pagePool.push_back(page);
	}

	return page;
}

void UploadBuffer::Reset()
{
	// can only be reset if all the allocations made
	// are no longer in fight in the command queue
	m_currentPage = nullptr;
	
	// Reset all available pages
	m_availablePages = m_pagePool;
	for (auto page : m_availablePages)
	{
		page->Reset();
	}
}

UploadBuffer::Page::Page(size_t sizeInBytes)
	: m_pageSize(sizeInBytes)
	, m_offset(0)
	, m_pCPU(nullptr)
	, m_pGPU(D3D12_GPU_VIRTUAL_ADDRESS(0))
{
	auto device = Application::Get().GetDevice();

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC desc(CD3DX12_RESOURCE_DESC::Buffer(m_pageSize));
	ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_resource)));

	m_pGPU = m_resource->GetGPUVirtualAddress();
	m_resource->Map(0, nullptr, &m_pCPU);

}

UploadBuffer::Page::~Page()
{
	m_resource->Unmap(0, nullptr);
	m_pCPU = nullptr;
	m_pGPU = D3D12_GPU_VIRTUAL_ADDRESS(0);
}

bool UploadBuffer::Page::HasSpace(size_t sizeInBytes, size_t alignment)
{
	// find the smallest value that is a multiple of the alignment (eg 13,8 would return 16)
	// essentially makes sure that the size you allocate meets the alignment
	size_t alignedSize = Math::AlignUp(sizeInBytes, alignment);
	// bumps the current offset to match the alignment. (eg offset is 260 but alignment is 256, puts offset at 512)
	size_t alignedOffset = Math::AlignUp(m_offset, alignment);
	return alignedOffset + alignedSize <= m_pageSize;
}

UploadBuffer::Allocation UploadBuffer::Page::Allocate(size_t sizeInBytes, size_t alignment)
{
	// WARNING: This function is not thread safe. 
	// If you use the same UploadBuffer instance across threads you need to take this into consideration
	if (!HasSpace(sizeInBytes, alignment))
	{
		// no space on the page
		throw std::bad_alloc();
	}

	size_t alignedSize = Math::AlignUp(sizeInBytes, alignment);
	m_offset = Math::AlignUp(m_offset, alignment);

	// m_pCPU has the adress of the mapped GPU adress given to it when creating the page
	Allocation allocation;
	allocation.CPU = static_cast<uint8_t*>(m_pCPU) + m_offset;
	allocation.GPU = m_pGPU + m_offset;

	m_offset += alignedSize;

	return allocation;
}

void UploadBuffer::Page::Reset()
{
	m_offset = 0;
}

