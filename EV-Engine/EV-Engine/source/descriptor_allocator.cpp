#include "descriptor_allocation.h"

#include "dx12_includes.h"

#include "descriptor_allocator.h"
#include "descriptor_allocator_page.h"

DescriptorAllocator::DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorsPerHeap)
	:m_heapType(type)
	, m_numDescriptorsPerHeap(descriptorsPerHeap)
{
}

DescriptorAllocator::~DescriptorAllocator()
{

}

std::shared_ptr<DescriptorAllocatorPage> DescriptorAllocator::CreateAllocatorPage()
{
	auto page = std::make_shared<DescriptorAllocatorPage>(m_heapType, m_numDescriptorsPerHeap);
	m_heapPool.emplace_back(page);
	m_availableHeaps.insert(m_heapPool.size() - 1);
	return page;
}

DescriptorAllocation DescriptorAllocator::Allocate(uint32_t numDescriptors)
{
	std::lock_guard<std::mutex> lock(m_allocationMutex);
	DescriptorAllocation allocation;

    auto iter = m_availableHeaps.begin();
    while (iter != m_availableHeaps.end())
    {
        auto allocatorPage = m_heapPool[*iter];

        allocation = allocatorPage->Allocate(numDescriptors);

        if (allocatorPage->GetNumFreeHandles() == 0)
        {
            iter = m_availableHeaps.erase(iter);
        }
        else
        {
            ++iter;
        }

        // A valid allocation has been found.
        if (!allocation.IsNull())
        {
            break;
        }
    }

    // No available heap could satisfy the requested number of descriptors.
    if (allocation.IsNull())
    {
        m_numDescriptorsPerHeap = std::max(m_numDescriptorsPerHeap, numDescriptors);
        auto newPage = CreateAllocatorPage();

        allocation = newPage->Allocate(numDescriptors);
    }

    return allocation;
}

void DescriptorAllocator::ReleaseStaleDescriptors()
{
	std::lock_guard<std::mutex> lock(m_allocationMutex);

	for (size_t i = 0; i < m_heapPool.size(); ++i)
	{
		auto page = m_heapPool[i];

		page->ReleaseStaleDescriptors();

		if (page->GetNumFreeHandles() > 0)
		{
			m_availableHeaps.insert(i);
		}
	}
}