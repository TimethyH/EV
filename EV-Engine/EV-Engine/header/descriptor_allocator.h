#pragma once

#include "d3dx12.h"

#include <cstdint>
#include <mutex>
#include <memory>
#include <set>
#include <vector>

class DescriptorAllocatorPage;
class DescriptorAllocator
{
public:

	DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorsPerHeap = 256);
	virtual ~DescriptorAllocator();

	DescriptorAllocation Allocate(uint32_t numDescriptors = 1);
	// Release stale descriptors after a frame ended
	void ReleaseStaleDescriptors(uint64_t frameNr);


private:
	using DescriptorHeapPool = std::vector<std::shared_ptr<DescriptorAllocatorPage>>;

	// Create new heap with specific amount of descriptors
	std::shared_ptr<DescriptorAllocatorPage> CreateAllocatorPage();

	D3D12_DESCRIPTOR_HEAP_TYPE m_heapType = {};
	uint32_t m_numDescriptorsPerHeap = {};
	DescriptorHeapPool m_heapPool = {};
	std::set<size_t> m_availableHeaps = {};
	std::mutex m_allocationMutex = {};

};