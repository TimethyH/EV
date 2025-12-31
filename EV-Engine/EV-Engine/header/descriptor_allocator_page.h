#pragma once

#include "descriptor_allocation.h"

#include <d3d12.h>

#include <wrl.h>

#include <map>
#include <memory>
#include <mutex>
#include <queue>

class DescriptorAllocatorPage : public std::enable_shared_from_this<DescriptorAllocatorPage>
{
public:
	DescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);

	D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType() const;
	bool HasSpace(uint32_t numDescriptors) const;
	uint32_t GetNumFreeHandles() const;
	DescriptorAllocation Allocate(uint32_t numDescriptors);

	void Free(DescriptorAllocation&& descriptor, uint32_t frame);
	void ReleaseStaleDescriptors();
protected:

	// Computes the offset of the descriptor handle from the start of the heap.
	uint32_t ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle);

	// Adds a new block to the free list.
	void AddNewBlock(uint32_t offset, uint32_t numDescriptors);

	// Free a block of descriptors.
	// This will also merge free blocks in the free list to form larger blocks that can be reused.
	void FreeBlock(uint32_t offset, uint32_t numDescriptors);

private:
	// The offset (in descriptors) within the descriptor heap.
	using OffsetType = uint32_t;
	// The number of descriptors that are available.
	using SizeType = uint32_t;

	struct FreeBlockInfo;
	// A map that lists the free blocks by the offset within the descriptor heap.
	using FreeListByOffset = std::map<OffsetType, FreeBlockInfo>;

	// A map that lists the free blocks by size.
	// Needs to be a multimap since multiple blocks can have the same size.
	using FreeListBySize = std::multimap<SizeType, FreeListByOffset::iterator>;

	struct FreeBlockInfo
	{
		FreeBlockInfo(SizeType size)
			: Size(size)
		{
		}

		SizeType Size;
		FreeListBySize::iterator FreeListBySizeIt;
	};

	struct StaleDescriptorInfo
	{
		StaleDescriptorInfo(OffsetType offset, SizeType size, uint64_t frame)
			: offset(offset)
			, size(size)
			, frameNumber(frame)
		{
		}

		// The offset within the descriptor heap.
		OffsetType offset;
		// The number of descriptors
		SizeType size;
		// The frame number that the descriptor was freed.
		uint64_t frameNumber;
	};

	// Stale descriptors are queued for release until the frame that they were freed
	// has completed.
	using StaleDescriptorQueue = std::queue<StaleDescriptorInfo>;

	FreeListByOffset m_freeListByOffset;
	FreeListBySize m_freeListBySize;
	StaleDescriptorQueue m_staleDescriptors;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
	D3D12_DESCRIPTOR_HEAP_TYPE m_heapType;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_baseDescriptor;
	uint32_t m_descriptorHandleIncrementSize;
	uint32_t m_numDescriptorsInHeap;
	uint32_t m_numFreeHandles;

	std::mutex m_allocationMutex;
};