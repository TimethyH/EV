#pragma once

#include <d3d12.h> // For CommandQueue, Device and Fence
#include <wrl.h> // For ComPtr<>

#include <cstdint>
#include <queue>

class CommandQueue
{
public:
	CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device13> pDevice, D3D12_COMMAND_LIST_TYPE type);
	virtual ~CommandQueue();

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> GetCommandList();

	uint64_t ExecuteCommandList(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList);

	uint64_t Signal();
	bool IsFenceComplete(uint64_t fenceValue);
	void WaitForFenceValue(uint64_t fenceValue);
	void Flush();

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

protected:
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CreateCommandAllocator();
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> CreateCommandList(Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator);

private:
	// Keep track of command allocators in flight
	struct CommandAllocatorEntry
	{
		uint64_t fenceValue;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	};

	using CommandAllocatorQueue = std::queue<CommandAllocatorEntry>;
	using CommandListQueue = std::queue < Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> >;

	D3D12_COMMAND_LIST_TYPE m_commandListType = {};
	Microsoft::WRL::ComPtr<ID3D12Device13> m_d3d12Device = {};
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue = {};
	Microsoft::WRL::ComPtr<ID3D12Fence> m_d3d12Fence = {};
	HANDLE m_fenceEvent = {};
	uint64_t m_fenceValue = 0;

	CommandAllocatorQueue m_commandAllocatorQueue = {};
	CommandListQueue	m_commandListQueue = {};
};

