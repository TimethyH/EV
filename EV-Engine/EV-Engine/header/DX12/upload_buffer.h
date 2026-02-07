#pragma once

#include "utility/defines.h"

#include <wrl.h>
#include <d3d12.h>
#include <memory>
#include <deque>

namespace EV
{

	class UploadBuffer
	{
	public:
		struct Allocation
		{
			void* CPU;
			D3D12_GPU_VIRTUAL_ADDRESS GPU;
		};

		explicit UploadBuffer(size_t pageSize = _2MB);

		size_t GetPageSize() const;

		Allocation Allocate(size_t sizeInBytes, size_t alignment);
		void Reset();

	private:
		struct Page
		{
			Page(size_t sizeInBytes);
			~Page();
			bool HasSpace(size_t sizeInBytes, size_t alignment);
			Allocation Allocate(size_t sizeInBytes, size_t alignment);
			void Reset();

		private:
			// holds the GPU memory
			Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;

			// base pointer
			void* m_pCPU = nullptr;
			D3D12_GPU_VIRTUAL_ADDRESS m_pGPU;

			// Allocated page size
			size_t m_pageSize = 0;
			// Current allocation offset
			size_t m_offset = 0;

		};

		using PagePool = std::deque<std::shared_ptr<Page>>;
		std::shared_ptr<Page> RequestPage();

		size_t m_pageSize = 0;
		PagePool m_pagePool = {};
		PagePool m_availablePages = {};

		std::shared_ptr<Page> m_currentPage = nullptr;


	};
}